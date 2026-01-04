# NOTE: Make sure ElevateMe.exe is in the script directory before you run this script!

# conditionally prompt the user for UAC confirmation and restart the script with elevated privileges
Using namespace System.Security.Principal
If (-not ([WindowsPrincipal][WindowsIdentity]::GetCurrent()).IsInRole([WindowsBuiltInRole]::Administrator)) {
  Start-Process (Get-Process -Id $PID).Path "-NoP -File `"$PSCommandPath`"" -Verb RunAs
  Exit
}

$task = "ElevateMe"
$root = "$Env:USERPROFILE\AppData\Local\$task"
$rootEnvStr = "%USERPROFILE%\AppData\Local\$task"
# copy ElevateMe.exe from the script directory
$Null = New-Item $root -ItemType 'Directory' -EA SilentlyContinue
Copy-Item "$PSScriptRoot\$task.exe" "$root\" -Force
# create the task that runs ElevateMe.exe with elevated privileges
$act = New-ScheduledTaskAction -Ex "`"$root\$task.exe`"" -Arg '$(Arg0)'
$prn = New-ScheduledTaskPrincipal -U $Env:USERNAME -RunL Highest
$set = New-ScheduledTaskSettingsSet -AllowStartIfOnBat -Multi Parallel -Prio 4
$Null = Register-ScheduledTask -Force -TaskN $task -Act $act -Prin $prn -Set $set
# read the Path environment of the current user
$hkcuEnv = 'registry::HKEY_CURRENT_USER\Environment'
$paths = (Get-Item -LiteralPath $hkcuEnv).GetValue('Path', '', 'DoNotExpandEnvironmentNames') -split ';' -ne ''
# conditionally add the path to ElevateMe.exe and refresh the explorer environment (by broadcasting a WM_SETTINGCHANGE message)
If ($paths -contains $rootEnvStr) { Exit }
$newPathStr = ($paths + $rootEnvStr) -join ';'
Set-ItemProperty -Type ExpandString -LiteralPath $hkcuEnv 'Path' $newPathStr
$Null = (Add-Type -Name W '[DllImport("user32")] public static extern IntPtr SendMessageTimeout(IntPtr h, int m, IntPtr w, string l, int f, int t, int r);' -PassThru)::SendMessageTimeout([IntPtr]65535, 26, [IntPtr]::Zero, 'Environment', 10, 0, 0)
