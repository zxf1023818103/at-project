// Native Windows QUIC perf server (System.Net.Quic / msquic) for the AmebaD
// QUIC LAN speed test. Speaks the same "perf" protocol as the chip's ngtcp2
// client: ALPN "perf"; the client opens a bidi stream, sends an 8-byte
// big-endian uint64 = bytes to receive (download), then streams upload bytes
// and FINs. The server returns exactly that many bytes + FIN and counts the
// upload. Runs natively on Windows so it binds the mobile-hotspot IP
// (192.168.137.1) the chip can reach.
//
//   dotnet run -- 0.0.0.0 4433
using System.Buffers.Binary;
using System.Diagnostics;
using System.Net;
using System.Net.Quic;
using System.Net.Security;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

#pragma warning disable SYSLIB5001 // System.Net.Quic experimental APIs

if (!QuicListener.IsSupported)
{
    Console.Error.WriteLine("QUIC is not supported on this OS/runtime (need Win11 22H2+ with Schannel TLS1.3).");
    return 1;
}

string bindIp = args.Length > 0 ? args[0] : "0.0.0.0";
int port = args.Length > 1 ? int.Parse(args[1]) : 4433;

// Self-signed ECDSA P-256 cert (the chip runs SSL_VERIFY_NONE). Re-import via
// PFX so Schannel can use the private key.
using var ecdsa = ECDsa.Create(ECCurve.NamedCurves.nistP256);
var req = new CertificateRequest("CN=quicperf", ecdsa, HashAlgorithmName.SHA256);
using var tmp = req.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddYears(1));
// Schannel (QUIC server credential) needs a persisted private key it can open;
// EphemeralKeySet keys are not accessible to Schannel -> QUIC_STATUS_CERT_NO_CERT.
var serverCert = new X509Certificate2(tmp.Export(X509ContentType.Pfx), (string?)null,
                     X509KeyStorageFlags.Exportable | X509KeyStorageFlags.PersistKeySet |
                     X509KeyStorageFlags.UserKeySet);

var alpn = new SslApplicationProtocol("perf");

var listenerOptions = new QuicListenerOptions
{
    ListenEndPoint = new IPEndPoint(IPAddress.Parse(bindIp), port),
    ApplicationProtocols = new List<SslApplicationProtocol> { alpn },
    ConnectionOptionsCallback = (_, _, _) => ValueTask.FromResult(new QuicServerConnectionOptions
    {
        DefaultStreamErrorCode = 0x10c,
        DefaultCloseErrorCode = 0x100,
        ServerAuthenticationOptions = new SslServerAuthenticationOptions
        {
            ApplicationProtocols = new List<SslApplicationProtocol> { alpn },
            ServerCertificate = serverCert,
        },
    }),
};

await using var listener = await QuicListener.ListenAsync(listenerOptions);
Console.WriteLine($"perf server (System.Net.Quic) on {bindIp}:{port} ALPN \"perf\" — waiting...");

while (true)
{
    QuicConnection conn;
    try { conn = await listener.AcceptConnectionAsync(); }
    catch (Exception e) { Console.Error.WriteLine($"accept: {e.Message}"); continue; }
    _ = HandleConnectionAsync(conn);
}

static async Task HandleConnectionAsync(QuicConnection conn)
{
    Console.WriteLine($"conn from {conn.RemoteEndPoint}");
    try
    {
        while (true)
        {
            var stream = await conn.AcceptInboundStreamAsync();
            _ = HandleStreamAsync(stream);
        }
    }
    catch { /* connection closed */ }
}

static async Task HandleStreamAsync(QuicStream s)
{
    ulong download = 0, sent = 0;
    long upload = 0;
    var sw = Stopwatch.StartNew();
    string note = "ok";
    try
    {
        // 8-byte big-endian download length
        var hdr = new byte[8];
        int got = 0;
        while (got < 8)
        {
            int r = await s.ReadAsync(hdr.AsMemory(got, 8 - got));
            if (r == 0) break;
            got += r;
        }
        if (got < 8) return;
        download = BinaryPrimitives.ReadUInt64BigEndian(hdr);
        sw.Restart();

        // sink the upload (rest of the stream) concurrently with sending
        var sink = Task.Run(async () =>
        {
            try { var buf = new byte[1 << 16]; int r; while ((r = await s.ReadAsync(buf)) > 0) upload += r; }
            catch { /* peer closed */ }
        });

        // send the requested download bytes, then FIN
        if (download > 0)
        {
            var zero = new byte[1 << 16];
            while (sent < download)
            {
                int chunk = (int)Math.Min((ulong)zero.Length, download - sent);
                await s.WriteAsync(zero.AsMemory(0, chunk));
                sent += (ulong)chunk;
            }
            s.CompleteWrites();
        }
        await sink;
    }
    catch (Exception e) { note = "aborted: " + e.Message; }
    sw.Stop();

    double ms = sw.Elapsed.TotalMilliseconds; if (ms < 1) ms = 1;
    if (download > 0)
        Console.WriteLine($"  DOWNLOAD (chip RX): {sent}/{download} B in {ms:F1} ms = {sent * 8.0 / ms / 1e3:F2} Mbps [{note}]");
    if (upload > 0 || download == 0)
        Console.WriteLine($"  UPLOAD   (chip TX): {upload} B in {ms:F1} ms = {upload * 8.0 / ms / 1e3:F2} Mbps [{note}]");
    try { await s.DisposeAsync(); } catch { }
}
