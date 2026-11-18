param([switch]$Scan, [string]$DeviceAddress="B4E8423CB52B")
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new()
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class PanBluetooth {
 [StructLayout(LayoutKind.Sequential)] public struct FindParams { public uint size; }
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
 public struct RadioInfo {
  public uint size; public ulong address;
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=248)] public string name;
  public uint deviceClass; public ushort lmp; public ushort manufacturer;
 }
 [StructLayout(LayoutKind.Sequential)] public struct Time {
  public ushort year,month,weekday,day,hour,minute,second,millis;
 }
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
 public struct DeviceInfo {
  public uint size; public ulong address; public uint deviceClass;
  [MarshalAs(UnmanagedType.Bool)] public bool connected;
  [MarshalAs(UnmanagedType.Bool)] public bool remembered;
  [MarshalAs(UnmanagedType.Bool)] public bool authenticated;
  public Time lastSeen,lastUsed;
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=248)] public string name;
 }
 [DllImport("bthprops.cpl",SetLastError=true)] public static extern IntPtr BluetoothFindFirstRadio(ref FindParams p,out IntPtr radio);
 [DllImport("bthprops.cpl")] public static extern bool BluetoothFindRadioClose(IntPtr find);
 [DllImport("bthprops.cpl")] public static extern uint BluetoothGetRadioInfo(IntPtr radio,ref RadioInfo info);
 [DllImport("bthprops.cpl")] public static extern bool BluetoothIsConnectable(IntPtr radio);
 [DllImport("bthprops.cpl")] public static extern bool BluetoothIsDiscoverable(IntPtr radio);
 [DllImport("bthprops.cpl")] public static extern uint BluetoothGetDeviceInfo(IntPtr radio,ref DeviceInfo info);
 [StructLayout(LayoutKind.Sequential)] public struct SearchParams {
  public uint size;
  [MarshalAs(UnmanagedType.Bool)] public bool authenticated;
  [MarshalAs(UnmanagedType.Bool)] public bool remembered;
  [MarshalAs(UnmanagedType.Bool)] public bool unknown;
  [MarshalAs(UnmanagedType.Bool)] public bool connected;
  [MarshalAs(UnmanagedType.Bool)] public bool inquiry;
  public byte timeout; public IntPtr radio;
 }
 [DllImport("bthprops.cpl",SetLastError=true)] public static extern IntPtr BluetoothFindFirstDevice(ref SearchParams search,ref DeviceInfo device);
 [DllImport("bthprops.cpl")] public static extern bool BluetoothFindNextDevice(IntPtr find,ref DeviceInfo device);
 [DllImport("bthprops.cpl")] public static extern bool BluetoothFindDeviceClose(IntPtr find);
 public static void Discover(IntPtr radio,ulong target) {
  SearchParams search=new SearchParams(); search.size=(uint)Marshal.SizeOf(search);
  search.authenticated=search.remembered=search.unknown=search.connected=search.inquiry=true;
  search.timeout=4; search.radio=radio;
  DeviceInfo device=new DeviceInfo(); device.size=(uint)Marshal.SizeOf(device);
  IntPtr find=BluetoothFindFirstDevice(ref search,ref device);
  if(find==IntPtr.Zero) { Console.WriteLine("INQUIRY_ERROR="+Marshal.GetLastWin32Error()); return; }
  int count=0; bool found=false;
  try { do { count++; if(device.address==target){found=true;Console.WriteLine("FOUND_BOARD="+device.name);} } while(BluetoothFindNextDevice(find,ref device)); }
  finally { BluetoothFindDeviceClose(find); }
  Console.WriteLine("INQUIRY_COUNT="+count+" BOARD_FOUND="+found);
 }
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
$p = [PanBluetooth+FindParams]::new()
$p.size = [Runtime.InteropServices.Marshal]::SizeOf($p)
$radio = [IntPtr]::Zero
$find = [PanBluetooth]::BluetoothFindFirstRadio([ref]$p,[ref]$radio)
if ($find -eq [IntPtr]::Zero) { throw 'No Bluetooth radio' }
try {
 $info = [PanBluetooth+RadioInfo]::new()
 $info.size = [Runtime.InteropServices.Marshal]::SizeOf($info)
 $status = [PanBluetooth]::BluetoothGetRadioInfo($radio,[ref]$info)
 if ($status) { throw "BluetoothGetRadioInfo=$status" }
 'RADIO_ADDRESS='+$info.address.ToString('X12')
 'CONNECTABLE='+[PanBluetooth]::BluetoothIsConnectable($radio)
 'DISCOVERABLE='+[PanBluetooth]::BluetoothIsDiscoverable($radio)
 $device = [PanBluetooth+DeviceInfo]::new()
 $device.size = [Runtime.InteropServices.Marshal]::SizeOf($device)
 $device.address = [Convert]::ToUInt64(($DeviceAddress -replace "[:-]",""),16)
 $status = [PanBluetooth]::BluetoothGetDeviceInfo($radio,[ref]$device)
 "DEVICE_QUERY=$status CONNECTED=$($device.connected) AUTHENTICATED=$($device.authenticated)"
 if ($Scan) { [PanBluetooth]::Discover($radio,$device.address) }
} finally {
 [void][PanBluetooth]::BluetoothFindRadioClose($find)
 [void][PanBluetooth]::CloseHandle($radio)
}
