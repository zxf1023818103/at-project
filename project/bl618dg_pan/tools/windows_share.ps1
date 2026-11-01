# Select the actual routed public adapter, including a Hyper-V vEthernet NIC.
# Query by default. With -Enable, first turn Mobile hotspot off in Settings.
param([Parameter(Mandatory=$true)][string]$PublicInterfaceAlias, [switch]$Enable,
      [ValidateSet("Keep","Disable","Enable")][string]$IdleTimeout="Keep")
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
[void][Windows.Networking.Connectivity.NetworkInformation,Windows.Networking.Connectivity,ContentType=WindowsRuntime]
[void][Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager,Windows.Networking.NetworkOperators,ContentType=WindowsRuntime]
[void][Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult,Windows.Networking.NetworkOperators,ContentType=WindowsRuntime]
$publicAdapter=Get-NetAdapter -Name $PublicInterfaceAlias | Select-Object -First 1
$panAdapter=Get-NetAdapter | Where-Object {$_.InterfaceDescription -like 'Bluetooth*Personal Area Network*'} | Select-Object -First 1
if(!$publicAdapter -or !$panAdapter){throw 'Public or Bluetooth PAN adapter not found'}
$profiles=[Windows.Networking.Connectivity.NetworkInformation]::GetConnectionProfiles()
$public=$profiles | Where-Object {$_.NetworkAdapter.NetworkAdapterId -eq [guid]$publicAdapter.InterfaceGuid} | Select-Object -First 1
$private=$profiles | Where-Object {$_.NetworkAdapter.NetworkAdapterId -eq [guid]$panAdapter.InterfaceGuid} | Select-Object -First 1
if(!$public -or !$private){throw 'Public or Bluetooth PAN connection profile not found'}
$manager=[Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::CreateFromConnectionProfile($public,$private.NetworkAdapter)
'PUBLIC='+$publicAdapter.Name+' CONNECTIVITY='+$public.GetNetworkConnectivityLevel()
'PRIVATE='+$panAdapter.Name+' STATE='+$manager.TetheringOperationalState
if($IdleTimeout -eq 'Disable'){
 [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::DisableNoConnectionsTimeout()
}elseif($IdleTimeout -eq 'Enable'){
 [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::EnableNoConnectionsTimeout()
}
'IDLE_TIMEOUT_ENABLED='+[Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::IsNoConnectionsTimeoutEnabled()
if($Enable){
 if($manager.TetheringOperationalState -ne 'Off'){throw 'Turn Mobile hotspot off before changing its public adapter'}
 $operation=$manager.StartTetheringAsync()
 $method=[System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {$_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'} | Select-Object -First 1
 $task=$method.MakeGenericMethod([Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult]).Invoke($null,@($operation))
 if(!$task.Wait(20000)){throw 'Tethering start timed out; check Settings for resulting state'}
 if($task.Result.Status -ne 'Success'){throw ('Tethering failed: '+$task.Result.Status+' '+$task.Result.AdditionalErrorMessage)}
 'STATE='+$manager.TetheringOperationalState
}
