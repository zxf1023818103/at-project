#!/usr/bin/env python3
"""Temporary RouterOS wifi2 station/NAT fixture; saves and restores Wi-Fi config."""
import argparse
import datetime
import json
import pathlib
import re
import subprocess

TAG = "bl618dg-ap-test"


def quote(value):
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"').replace('$', '\\$').replace('\n', '\\n') + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("setup", "status", "restore"))
    parser.add_argument("--router", default="admin@192.168.100.2")
    parser.add_argument("--out", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1]/"build/validation/softap")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    def ros(command):
        result = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=6",
                                 args.router, command], capture_output=True, text=True, timeout=35)
        output = result.stdout + result.stderr
        if result.returncode or re.search(r"syntax error|failure:|bad command name|expected end of command|input does not match", output, re.I):
            raise RuntimeError(output)
        return output

    if args.action == "status":
        print(ros('/interface wifi print detail where name=wifi2; /interface wifi registration-table print detail where interface=wifi2; /ip dhcp-client print detail where comment="'+TAG+'"; /system resource print'))
    elif args.action == "restore":
        command = (args.out/"router-restore.rsc").read_text()
        print(ros(command))
        print(ros('/system script remove [find name="'+TAG+'-restore"]'))
        print(ros('/interface wifi export terse'))
    else:
        original = ros('/interface wifi export terse')
        (args.out/"router-wifi-before.rsc").write_text(original)
        line = next((l for l in original.splitlines() if l.startswith('/interface wifi set [ find default-name=wifi2 ]')), None)
        if not line:
            raise RuntimeError("No physical wifi2 export entry")
        registrations = ros('/interface wifi registration-table print detail where interface=wifi2')
        if "mac-address=" in registrations:
            raise RuntimeError("wifi2 has clients; select an unused radio")
        if ros('/interface wifi security find name="'+TAG+'"').strip():
            raise RuntimeError("Test security profile already exists; restore previous fixture first")
        restore = '\n'.join([
            '/interface wifi disable [find name="wifi2"]',
            '/ip dhcp-client remove [find comment="'+TAG+'"]',
            '/ip firewall nat remove [find comment="'+TAG+'"]',
            '/interface wifi reset [find default-name="wifi2"]',
            line,
            '/interface list member remove [find comment="'+TAG+'"]',
            '/interface wifi enable [find name="wifi-attest"]',
            '/interface wifi enable [find name="wifi-ateap"]',
            '/interface wifi security remove [find name="'+TAG+'"]',
            '/system scheduler remove [find name="'+TAG+'-rollback"]',
        ])
        (args.out/"router-restore.rsc").write_text(restore)
        clock = ros(':put [/system clock get date]; :put [/system clock get time]').splitlines()
        deadline = datetime.datetime.fromisoformat(clock[0]+"T"+clock[1])+datetime.timedelta(hours=1)
        ros('/system script add name="'+TAG+'-restore" policy=read,write,test,policy source='+quote(restore))
        ros('/system scheduler add name="'+TAG+'-rollback" start-date='+deadline.strftime('%Y-%m-%d')+
            ' start-time='+deadline.strftime('%H:%M:%S')+' interval=0 on-event="/system script run '+TAG+'-restore" policy=read,write,test,policy')
        commands = [
            '/interface wifi disable [find name="wifi2"]',
            '/interface wifi disable [find name="wifi-attest"]',
            '/interface wifi disable [find name="wifi-ateap"]',
        ]
        for interface in ('wifi2','wifi-attest','wifi-ateap'):
            commands.append('/interface list member add list=s2s-dedicated interface='+interface+' comment="'+TAG+'"')
        commands += [
            '/interface wifi security add name="'+TAG+'" authentication-types=wpa2-psk passphrase="bl618dg-test"',
            '/interface wifi set [find name="wifi2"] configuration.mode=station configuration.ssid="BL618DG-AP-Test" security="'+TAG+'" channel.frequency=5180 channel.width=20mhz disabled=no',
            '/ip dhcp-client add interface=wifi2 add-default-route=no use-peer-dns=no use-peer-ntp=no comment="'+TAG+'" disabled=no',
            '/ip firewall nat add chain=dstnat src-address=192.168.100.6/32 dst-address=192.168.100.2 protocol=tcp dst-port=18080 action=dst-nat to-addresses=192.168.77.1 to-ports=8080 comment="'+TAG+'"',
            '/ip firewall nat add chain=srcnat src-address=192.168.100.6/32 dst-address=192.168.77.1 protocol=tcp dst-port=8080 out-interface=wifi2 action=masquerade comment="'+TAG+'"',
        ]
        (args.out/"router-setup.rsc").write_text('\n'.join(commands))
        try:
            for command in commands:
                ros(command)
        except Exception:
            ros(restore)
            raise
        print(json.dumps({"station": "wifi2", "forward": "192.168.100.2:18080 -> 192.168.77.1:8080",
                          "rollback": deadline.isoformat(), "backup": str(args.out/"router-wifi-before.rsc")}))


if __name__ == "__main__":
    main()
