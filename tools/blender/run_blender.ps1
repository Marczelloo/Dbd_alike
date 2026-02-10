param(
  [Parameter(Mandatory=$true)][string]$Script,
  [string[]]$Args = @()
)

$blender = $env:BLENDER_PATH
if (-not $blender) { $blender = "blender" }

$scriptPath = Resolve-Path $Script
$argLine = @("-b", "-P", $scriptPath, "--") + $Args

& $blender @argLine
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
