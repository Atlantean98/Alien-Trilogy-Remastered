param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$source=Join-Path $projectRoot 'decompilation/cpp/CoopSecureTransport.cs'
if(!$OutputDirectory){$OutputDirectory=Join-Path $projectRoot 'decompilation/cpp/build'}
$target=Join-Path ([System.IO.Path]::GetFullPath($OutputDirectory)) 'TrilogyCoopTransport.exe'
& 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe' /nologo /target:exe /reference:System.Security.dll "/out:$target" $source
if($LASTEXITCODE -ne 0){throw 'Secure co-op transport build failed'}
