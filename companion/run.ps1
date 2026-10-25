param([Parameter(ValueFromRemainingArguments = $true)] $Args)

$ErrorActionPreference = "Stop"
$env:PYTHONPATH = $PSScriptRoot
python -m cc_usage_display @Args
