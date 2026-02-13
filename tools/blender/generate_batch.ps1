param(
  [string]$Config = "config/assets.json",
  [switch]$Force
)

$cliArgs = @("generate", "--config", $Config)
if ($Force) {
  $cliArgs += "--force"
}

& "$PSScriptRoot\run_blender.ps1" -Script "$PSScriptRoot\scripts\cli.py" -Args $cliArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
