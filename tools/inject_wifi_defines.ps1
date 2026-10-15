# 从环境变量生成 ARMCC via 文件，供 Keil 以 -D 注入 WIFI_SSID / WIFI_PASSWD。
$ErrorActionPreference = 'Stop'

function ConvertTo-ArmccViaDefine {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Value
    )

    if ($Value -match "[\r\n]") {
        throw "$Name contains a newline."
    }

    # 先按 C 字符串转义，再按 ARMCC via 文件规则转义引号和反斜杠。
    $cEscaped = $Value.Replace('\', '\\').Replace('"', '\"')
    $viaEscaped = $cEscaped.Replace('\', '\\').Replace('"', '\"')
    return "-D ${Name}=\`"${viaEscaped}\`""
}

function Get-BuildEnv {
    param([Parameter(Mandatory = $true)][string]$Name)

    foreach ($target in @('Process', 'User', 'Machine')) {
        $value = [Environment]::GetEnvironmentVariable($Name, $target)
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            return $value
        }
    }
    return $null
}

function Write-BuildLog {
    param([Parameter(Mandatory = $true)][string]$Message)
    [Console]::Out.WriteLine($Message)
}

$ssid = Get-BuildEnv 'WIFI_SSID'
$passwd = Get-BuildEnv 'WIFI_PASSWD'

if ([string]::IsNullOrWhiteSpace($ssid) -or [string]::IsNullOrWhiteSpace($passwd)) {
    Write-BuildLog "ERROR: set environment variables WIFI_SSID and WIFI_PASSWD, then rebuild."
    exit 1
}

$lines = @(
    (ConvertTo-ArmccViaDefine -Name 'WIFI_SSID' -Value $ssid)
    (ConvertTo-ArmccViaDefine -Name 'WIFI_PASSWD' -Value $passwd)
)
$content = ($lines -join "`r`n") + "`r`n"

$viaPath = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\mdk\wifi_defines.via'))
$utf8 = New-Object System.Text.UTF8Encoding $false

$previous = $null
if (Test-Path -LiteralPath $viaPath) {
    $previous = [System.IO.File]::ReadAllText($viaPath)
}

if ($previous -ne $content) {
    [System.IO.File]::WriteAllText($viaPath, $content, $utf8)
    $objectDir = Join-Path (Split-Path -Parent $viaPath) 'Objects'
    foreach ($name in @('wifi.o', 'main.o', 'ui.o', 'main_page.o')) {
        $objectPath = Join-Path $objectDir $name
        if (Test-Path -LiteralPath $objectPath) {
            Remove-Item -LiteralPath $objectPath -Force
        }
    }
    Write-BuildLog "Injected WIFI_SSID and WIFI_PASSWD."
}

exit 0
