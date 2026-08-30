#requires -Version 5.1

[CmdletBinding()]
param()

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$script:Failures = New-Object 'System.Collections.Generic.List[string]'
$script:Warnings = New-Object 'System.Collections.Generic.List[string]'

function Add-Failure {
    param([Parameter(Mandatory = $true)][string]$Message)
    $script:Failures.Add($Message)
}

function Add-Warning {
    param([Parameter(Mandatory = $true)][string]$Message)
    $script:Warnings.Add($Message)
}

function Write-Section {
    param([Parameter(Mandatory = $true)][string]$Title)
    Write-Host ''
    Write-Host ('=== {0} ===' -f $Title)
}

function Show-Table {
    param([Parameter(Mandatory = $true)][object[]]$Rows)
    if ($Rows.Count -eq 0) {
        Write-Host '(no rows)'
        return
    }
    Write-Host (($Rows | Format-Table -AutoSize | Out-String -Width 280).TrimEnd())
}

function Normalize-Text {
    param([AllowNull()][string]$Text)
    if ($null -eq $Text) {
        return ''
    }
    return (($Text -replace "`r`n", "`n") -replace "`r", "`n")
}

function Normalize-Scalar {
    param([AllowNull()][string]$Value)
    if ($null -eq $Value) {
        return '<missing>'
    }
    return (($Value.Trim() -replace '\s+', ' '))
}

function Normalize-CIntegerToken {
    param([Parameter(Mandatory = $true)][string]$Value)
    return ((Normalize-Scalar $Value) -replace '[uUlL]+$', '')
}

function Get-HeadFile {
    param(
        [Parameter(Mandatory = $true)][string]$Repository,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $gitPath = $Path.Replace('\', '/')
    $output = @(& git -c ("safe.directory={0}" -f $Repository) -C $Repository show ('HEAD:{0}' -f $gitPath) 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "git show failed for ${gitPath}: $($output -join [Environment]::NewLine)"
    }
    return (Normalize-Text ($output -join "`n"))
}

function Get-CurrentFile {
    param(
        [Parameter(Mandatory = $true)][string]$Repository,
        [Parameter(Mandatory = $true)][string]$Path
    )
    return (Normalize-Text (Get-Content -LiteralPath (Join-Path $Repository $Path) -Raw))
}

function Get-RequiredCapture {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Label,
        [string]$Group = 'value'
    )

    $match = [regex]::Match($Text, $Pattern)
    if (-not $match.Success) {
        Add-Failure "Could not extract $Label"
        return '<missing>'
    }
    return (Normalize-Scalar $match.Groups[$Group].Value)
}

function Get-DefineValue {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Context
    )
    $pattern = '(?m)^[ \t]*#define[ \t]+' + [regex]::Escape($Name) + '[ \t]+(?<value>[^\r\n]+?)\s*$'
    return (Get-RequiredCapture -Text $Text -Pattern $pattern -Label "$Context define $Name")
}

function Get-IocValue {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Context
    )

    $pattern = '(?m)^' + [regex]::Escape($Name) + '=(?<value>[^\r\n]+)$'
    return (Get-RequiredCapture -Text $Text -Pattern $pattern -Label "$Context setting $Name")
}

function Get-CFunctionText {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Context
    )

    $signaturePattern = '(?m)^[ \t]*(?:static[ \t]+)?(?:[A-Za-z_][A-Za-z0-9_]*[ \t\*]+)+' +
        [regex]::Escape($Name) + '[ \t]*\([^;\r\n]*\)[ \t]*\r?$'
    $signature = [regex]::Match($Text, $signaturePattern)
    if (-not $signature.Success) {
        Add-Failure "Could not find function $Context::$Name"
        return ''
    }

    $openBrace = $Text.IndexOf('{', $signature.Index + $signature.Length)
    if ($openBrace -lt 0) {
        Add-Failure "Could not find opening brace for $Context::$Name"
        return ''
    }

    $depth = 0
    for ($index = $openBrace; $index -lt $Text.Length; $index++) {
        $character = $Text[$index]
        if ($character -eq '{') {
            $depth++
        }
        elseif ($character -eq '}') {
            $depth--
            if ($depth -eq 0) {
                return $Text.Substring($signature.Index, ($index - $signature.Index + 1))
            }
        }
    }

    Add-Failure "Could not find closing brace for $Context::$Name"
    return ''
}

function Get-TextFingerprint {
    param([Parameter(Mandatory = $true)][string]$Text)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes((Normalize-Text $Text))
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
    }
    finally {
        $sha.Dispose()
    }
}

function Compare-Map {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$Baseline,
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$Final,
        [bool]$FailOnDifference = $true,
        [string[]]$AllowedDifferenceKeys = @()
    )

    Write-Section $Title
    $keys = New-Object 'System.Collections.Generic.List[string]'
    foreach ($key in $Baseline.Keys) {
        $keys.Add([string]$key)
    }
    foreach ($key in $Final.Keys) {
        if (-not $keys.Contains([string]$key)) {
            $keys.Add([string]$key)
        }
    }

    $rows = @()
    foreach ($key in $keys) {
        $baselineValue = '<missing>'
        $finalValue = '<missing>'
        if ($Baseline.Contains($key)) {
            $baselineValue = [string]$Baseline[$key]
        }
        if ($Final.Contains($key)) {
            $finalValue = [string]$Final[$key]
        }
        $same = $baselineValue -ceq $finalValue
        $allowedDifference = (-not $same) -and ((-not $FailOnDifference) -or ($AllowedDifferenceKeys -contains $key))
        if ((-not $same) -and (-not $allowedDifference) -and $FailOnDifference) {
            Add-Failure "$Title changed: $key ('$baselineValue' -> '$finalValue')"
        }
        $rows += [pscustomobject]@{
            Item     = $key
            Baseline = $baselineValue
            Final    = $finalValue
            Status   = $(if ($same) { 'PASS' } elseif ($allowedDifference) { 'ALLOWED' } else { 'CHANGED' })
        }
    }
    Show-Table $rows
}

function Assert-ExactValue {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$Actual,
        [Parameter(Mandatory = $true)][string]$Expected
    )
    if ($Actual -cne $Expected) {
        Add-Failure "$Label must be '$Expected', found '$Actual'"
    }
}

function Get-AssignmentsMap {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$FunctionName,
        [Parameter(Mandatory = $true)][string]$ObjectName,
        [Parameter(Mandatory = $true)][string[]]$Fields,
        [Parameter(Mandatory = $true)][string]$Context
    )

    $block = Get-CFunctionText -Text $Text -Name $FunctionName -Context $Context
    $result = [ordered]@{}
    foreach ($field in $Fields) {
        $pattern = '(?m)^[ \t]*' + [regex]::Escape("$ObjectName.$field") +
            '[ \t]*=[ \t]*(?<value>[^;]+);'
        $result[$field] = Get-RequiredCapture -Text $block -Pattern $pattern -Label "$Context::$FunctionName $ObjectName.$field"
    }
    return $result
}

function Get-AdcSequenceMap {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$FunctionName,
        [Parameter(Mandatory = $true)][string]$Context
    )

    $block = Get-CFunctionText -Text $Text -Name $FunctionName -Context $Context
    $channel = '<unset>'
    $rank = '<unset>'
    $sampling = '<unset>'
    $order = 0
    $result = [ordered]@{}
    foreach ($line in ($block -split "`n")) {
        if ($line -match '^\s*sConfig\.Channel\s*=\s*(?<value>[^;]+);') {
            $channel = Normalize-Scalar $Matches.value
        }
        elseif ($line -match '^\s*sConfig\.Rank\s*=\s*(?<value>[^;]+);') {
            $rank = Normalize-Scalar $Matches.value
        }
        elseif ($line -match '^\s*sConfig\.SamplingTime\s*=\s*(?<value>[^;]+);') {
            $sampling = Normalize-Scalar $Matches.value
        }
        elseif ($line -match 'HAL_ADC_ConfigChannel\(&(?<handle>[A-Za-z0-9_]+),\s*&sConfig\)') {
            $order++
            $key = 'order {0} / rank {1}' -f $order, $rank
            $result[$key] = 'handle={0}; channel={1}; sampling={2}' -f $Matches.handle, $channel, $sampling
        }
    }
    if ($result.Count -eq 0) {
        Add-Failure "Could not extract ADC sequence from $Context::$FunctionName"
    }
    return $result
}

function Get-AuxAdcMap {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Context
    )

    $block = Get-CFunctionText -Text $Text -Name 'ReadAuxAnalogInputs' -Context $Context
    $result = [ordered]@{}
    $logicalOrder = @('Z', 'Y', 'X')

    foreach ($logicalName in $logicalOrder) {
        # Support both the original value-returning API and the status/out-parameter API.
        $directPattern = '(?m)^\s*adc_' + $logicalName.ToLowerInvariant() +
            '_raw\s*=\s*ADC2_ReadChannel\((?<value>ADC_CHANNEL_[0-9]+)\);'
        $direct = [regex]::Match($block, $directPattern)
        if ($direct.Success) {
            $result[('order {0}: {1}' -f ($result.Count + 1), $logicalName)] = Normalize-Scalar $direct.Groups['value'].Value
            continue
        }

        $outParameterPattern = '(?ms)ADC2_ReadChannel\(\s*(?<value>ADC_CHANNEL_[0-9]+)\s*,\s*&value\s*\)(?:(?!ADC2_ReadChannel).)*?' +
            'adc_' + $logicalName.ToLowerInvariant() + '_raw\s*=\s*value\s*;'
        $outParameter = [regex]::Match($block, $outParameterPattern)
        if ($outParameter.Success) {
            $result[('order {0}: {1}' -f ($result.Count + 1), $logicalName)] = Normalize-Scalar $outParameter.Groups['value'].Value
        }
        else {
            Add-Failure "Could not extract $Context auxiliary ADC channel for $logicalName"
        }
    }

    $batteryChannel = $null
    $directBattery = [regex]::Match($block, '(?m)^\s*adc_bat_raw\s*=\s*ADC2_ReadChannel\((?<value>ADC_CHANNEL_[0-9]+)\);')
    if ($directBattery.Success) {
        $batteryChannel = Normalize-Scalar $directBattery.Groups['value'].Value
    }
    else {
        $batteryBlock = Get-CFunctionText -Text $Text -Name 'Battery_ReadReliable' -Context $Context
        if ($batteryBlock.Length -gt 0) {
            $batteryChannel = Get-RequiredCapture -Text $batteryBlock -Pattern 'ADC2_ReadChannel\(\s*(?<value>ADC_CHANNEL_[0-9]+)\s*,' -Label "$Context battery ADC channel"
        }
    }
    if ($null -ne $batteryChannel) {
        $result['order 4: BAT'] = $batteryChannel
    }

    if ($result.Count -ne 4) {
        Add-Failure "$Context auxiliary ADC map must contain Z, Y, X, BAT in that order; extracted $($result.Count) entries"
    }
    return $result
}

function Get-GpioDefinitionMap {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Context
    )

    $result = [ordered]@{}
    $pinMatches = [regex]::Matches($Text, '(?m)^\s*#define\s+(?<name>[A-Za-z0-9_]+)_Pin\s+(?<pin>[^\s]+)\s*$')
    foreach ($pinMatch in $pinMatches) {
        $name = $pinMatch.Groups['name'].Value
        $portPattern = '(?m)^\s*#define\s+' + [regex]::Escape($name) + '_GPIO_Port\s+(?<value>[^\s]+)\s*$'
        $port = Get-RequiredCapture -Text $Text -Pattern $portPattern -Label "$Context GPIO port for $name"
        $result[$name] = 'port={0}; pin={1}' -f $port, (Normalize-Scalar $pinMatch.Groups['pin'].Value)
    }
    if ($result.Count -eq 0) {
        Add-Failure "No GPIO definitions extracted from $Context"
    }
    return $result
}

function Get-GpioInitMap {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$FunctionName,
        [Parameter(Mandatory = $true)][string]$Context,
        [Parameter(Mandatory = $true)][string]$SourceLabel
    )

    $block = Get-CFunctionText -Text $Text -Name $FunctionName -Context $Context
    $state = [ordered]@{
        Pin       = '<unset>'
        Mode      = '<unset>'
        Pull      = '<unset>'
        Speed     = '<unset>'
        Alternate = '<unset>'
    }
    $result = [ordered]@{}
    $index = 0
    foreach ($line in ($block -split "`n")) {
        if ($line -match '^\s*GPIO_InitStruct\.(?<field>Pin|Mode|Pull|Speed|Alternate)\s*=\s*(?<value>[^;]+);') {
            $state[$Matches.field] = Normalize-Scalar $Matches.value
        }
        elseif ($line -match 'HAL_GPIO_Init\((?<port>[^,]+),\s*&GPIO_InitStruct\)') {
            $index++
            $result[('{0} #{1}' -f $SourceLabel, $index)] =
                'port={0}; pins={1}; mode={2}; pull={3}; speed={4}; af={5}' -f
                (Normalize-Scalar $Matches.port), $state.Pin, $state.Mode, $state.Pull, $state.Speed, $state.Alternate
        }
    }
    if ($result.Count -eq 0) {
        Add-Failure "No GPIO initialization calls extracted from $Context::$FunctionName"
    }
    return $result
}

function Merge-Maps {
    param([Parameter(Mandatory = $true)][object[]]$Maps)
    $result = [ordered]@{}
    foreach ($map in $Maps) {
        foreach ($key in $map.Keys) {
            $result[$key] = $map[$key]
        }
    }
    return $result
}

function Get-RmsSemanticMap {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Context
    )

    $result = [ordered]@{}
    $rmsFunctionName = 'App_ProcessMeasurementWindow'
    if ([regex]::IsMatch($Text, '(?m)^\s*static\s+void\s+App_CalculateRmsWindow\s*\(')) {
        $rmsFunctionName = 'App_CalculateRmsWindow'
    }
    $block = Get-CFunctionText -Text $Text -Name $rmsFunctionName -Context $Context

    $voltageMap = 'ADC_IDX_VA, ADC_IDX_VB, ADC_IDX_VC'
    $currentMap = 'ADC_IDX_IA, ADC_IDX_IB, ADC_IDX_IC'
    if ($rmsFunctionName -eq 'App_CalculateRmsWindow') {
        $voltageMap = Get-RequiredCapture -Text $block -Pattern '(?ms)voltage_index\s*\[\s*3\s*\]\s*=\s*\{(?<value>.*?)\}' -Label "$Context voltage phase index array"
        $currentMap = Get-RequiredCapture -Text $block -Pattern '(?ms)current_index\s*\[\s*3\s*\]\s*=\s*\{(?<value>.*?)\}' -Label "$Context current phase index array"
    }
    $result['voltage phase index order'] = $voltageMap
    $result['current phase index order'] = $currentMap

    $rawExpressions = [ordered]@{
        'voltage mean'             = '(?m)^\s*float\s+mean_v\s*=\s*(?<value>[^;]+);'
        'voltage variance'         = '(?m)^\s*float\s+var_v\s*=\s*(?<value>[^;]+);'
        'current mean'             = '(?m)^\s*float\s+mean_i\s*=\s*(?<value>[^;]+);'
        'current variance'         = '(?m)^\s*float\s+var_i\s*=\s*(?<value>[^;]+);'
        'voltage RMS conversion'   = '(?m)^\s*rms_voltage\[i\]\s*=\s*(?<value>[^;]+);'
        'current RMS conversion'   = '(?m)^\s*rms_current\[i\]\s*=\s*(?<value>[^;]+);'
    }
    foreach ($label in $rawExpressions.Keys) {
        $expression = Get-RequiredCapture -Text $block -Pattern $rawExpressions[$label] -Label "$Context $label"
        # Canonicalize the original fixed offsets and the equivalent explicit ADC_IDX arrays.
        $expression = $expression -replace 'local_sum_sq\[i\s*\+\s*3U\]', 'I_SUM_SQ'
        $expression = $expression -replace 'sum_sq\[current_index\[i\]\]', 'I_SUM_SQ'
        $expression = $expression -replace 'local_sum\[i\s*\+\s*3U\]', 'I_SUM'
        $expression = $expression -replace 'sum\[current_index\[i\]\]', 'I_SUM'
        $expression = $expression -replace 'local_sum_sq\[i\]', 'V_SUM_SQ'
        $expression = $expression -replace 'sum_sq\[voltage_index\[i\]\]', 'V_SUM_SQ'
        $expression = $expression -replace 'local_sum\[i\]', 'V_SUM'
        $expression = $expression -replace 'sum\[voltage_index\[i\]\]', 'V_SUM'
        $result[$label] = Normalize-Scalar $expression
    }

    $result['negative variance handling'] = 'clamp var_v and var_i to 0.0f before sqrtf'
    foreach ($variance in @('var_v', 'var_i')) {
        if (-not [regex]::IsMatch($block, 'if\s*\(\s*' + $variance + '\s*<\s*0\.0f\s*\)\s*\{\s*' + $variance + '\s*=\s*0\.0f\s*;', 'Singleline')) {
            $result['negative variance handling'] = "missing clamp for $variance"
        }
    }

    $result['raw ADC to voltage'] = Get-RequiredCapture -Text $Text -Pattern '(?m)^\s*return\s+(?<value>\(\(float\)raw\s*\*\s*VREF\)\s*/\s*ADC_MAX)\s*;' -Label "$Context raw ADC scaling"
    $result['revolutions from pulses'] = Get-RequiredCapture -Text $Text -Pattern '(?m)^\s*revolutions\s*=\s*(?<value>[^;]+);' -Label "$Context revolutions formula"

    # Validate semantic accumulation of every channel in each completed DMA sequence.
    if ([regex]::IsMatch($Text, '(?m)^\s*static\s+void\s+App_ProcessAdcBlock\s*\(')) {
        $acquisition = Get-CFunctionText -Text $Text -Name 'App_ProcessAdcBlock' -Context $Context
        $orderedSequence = [regex]::IsMatch($acquisition,
            'base\s*=\s*(?:\(uint16_t\)\s*)?\(?\s*offset\s*\+\s*\(sequence\s*\*\s*ADC_CHANNELS\).*?' +
            'channel\s*=\s*0U.*?channel\s*<\s*ADC_CHANNELS.*?' +
            'raw\s*=\s*adc_buffer\[base\s*\+\s*channel\]', 'Singleline')
        $sumOk = [regex]::IsMatch($acquisition, 'sum\[channel\]\s*\+=\s*value\s*;')
        $sumSqOk = [regex]::IsMatch($acquisition, 'sum_sq\[channel\]\s*\+=\s*value\s*\*\s*value\s*;')
        $result['DMA sequence traversal'] = $(if ($orderedSequence) { 'ordered ADC_CHANNELS sequence' } else { 'invalid DMA sequence traversal' })
        $result['sum accumulation'] = $(if ($sumOk) { 'sum[channel] += sample' } else { 'invalid sum accumulation' })
        $result['square-sum accumulation'] = $(if ($sumSqOk) { 'sum_sq[channel] += sample * sample' } else { 'invalid square-sum accumulation' })
    }
    else {
        $legacyOrder = [regex]::IsMatch($Text, 'for\s*\([^\)]*i\s*=\s*0[^\)]*i\s*<\s*ADC_CHANNELS[^\)]*\).*?' +
            'adc_buffer\[i\].*?sum\[i\]\s*\+=\s*v.*?sum_sq\[i\]\s*\+=\s*v\s*\*\s*v', 'Singleline')
        $result['DMA sequence traversal'] = $(if ($legacyOrder) { 'ordered ADC_CHANNELS sequence' } else { 'invalid DMA sequence traversal' })
        $result['sum accumulation'] = $(if ($legacyOrder) { 'sum[channel] += sample' } else { 'invalid sum accumulation' })
        $result['square-sum accumulation'] = $(if ($legacyOrder) { 'sum_sq[channel] += sample * sample' } else { 'invalid square-sum accumulation' })
    }
    return $result
}

function Get-DmaShapeMap {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Context
    )

    $result = [ordered]@{}
    $result['HAL_ADC_Start_DMA length'] = Get-RequiredCapture -Text $Text -Pattern 'HAL_ADC_Start_DMA\(\s*&hadc1,\s*\(uint32_t\s*\*\)\s*(?:\(void\s*\*\)\s*)?adc_buffer,\s*(?<value>[^\)]+)\)' -Label "$Context ADC DMA length"
    $buffer = [regex]::Match($Text, '(?m)^\s*(?:_Alignas\([^\)]+\)\s*)?static\s+uint16_t\s+adc_buffer\s*\[\s*(?<value>[^\]]+)\s*\]')
    if ($buffer.Success) {
        $result['adc_buffer element count'] = Normalize-Scalar $buffer.Groups['value'].Value
    }
    else {
        Add-Failure "Could not extract $Context adc_buffer element count"
        $result['adc_buffer element count'] = '<missing>'
    }

    if ($result['HAL_ADC_Start_DMA length'] -eq 'ADC_DMA_BUFFER_VALUES') {
        $halfSequences = Get-DefineValue -Text $Text -Name 'ADC_DMA_HALF_SEQUENCES' -Context $Context
        $halfValues = Get-DefineValue -Text $Text -Name 'ADC_DMA_HALF_VALUES' -Context $Context
        $bufferValues = Get-DefineValue -Text $Text -Name 'ADC_DMA_BUFFER_VALUES' -Context $Context
        $result['half sequences'] = $halfSequences
        $result['half values formula'] = $halfValues
        $result['buffer values formula'] = $bufferValues
        Assert-ExactValue -Label "$Context ADC DMA half values formula" -Actual $halfValues -Expected '(ADC_CHANNELS * ADC_DMA_HALF_SEQUENCES)'
        Assert-ExactValue -Label "$Context ADC DMA buffer values formula" -Actual $bufferValues -Expected '(ADC_DMA_HALF_VALUES * 2U)'
        Assert-ExactValue -Label "$Context ADC DMA array/transfer length" -Actual $result['adc_buffer element count'] -Expected 'ADC_DMA_BUFFER_VALUES'
    }
    elseif ($result['HAL_ADC_Start_DMA length'] -eq 'ADC_CHANNELS') {
        $result['half sequences'] = 'not used (single sequence)'
        $result['half values formula'] = 'not used (single sequence)'
        $result['buffer values formula'] = 'ADC_CHANNELS'
        Assert-ExactValue -Label "$Context ADC DMA array/transfer length" -Actual $result['adc_buffer element count'] -Expected 'ADC_CHANNELS'
    }
    else {
        Add-Failure "$Context uses unsupported ADC DMA length '$($result['HAL_ADC_Start_DMA length'])'"
    }
    return $result
}

function Get-PacketMap {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Context
    )

    $result = [ordered]@{}
    $result['payload size'] = Get-DefineValue -Text $Text -Name 'RAK_MEAS_PAYLOAD_SIZE' -Context $Context
    $result['text buffer size'] = Get-DefineValue -Text $Text -Name 'RAK_PAYLOAD_TEXT_SIZE' -Context $Context
    $result['hex buffer formula'] = Get-DefineValue -Text $Text -Name 'RAK_PAYLOAD_HEX_SIZE' -Context $Context
    $result['AT command buffer formula'] = Get-DefineValue -Text $Text -Name 'RAK_SEND_CMD_SIZE' -Context $Context
    $fPort = Get-RequiredCapture -Text $Text -Pattern 'RAK_SendBytesPayload\(\s*(?<value>[^,]+),\s*payload\s*,\s*sizeof\(payload\)\s*\)' -Label "$Context RAK fPort"
    $result['fPort'] = Normalize-CIntegerToken $fPort
    $result['byte 0: format version'] = Get-RequiredCapture -Text $Text -Pattern 'payload\[\s*0\s*\]\s*=\s*(?<value>[^;]+);' -Label "$Context payload version"
    $result['bytes 1..14: value order'] = Get-RequiredCapture -Text $Text -Pattern '(?ms)const\s+float\s+values\[\]\s*=\s*\{(?<value>.*?)\};' -Label "$Context payload value order"
    $result['bytes 1..14: item count'] = Get-RequiredCapture -Text $Text -Pattern 'for\s*\([^;]+;\s*i\s*<\s*(?<value>[^;]+);[^\)]*\)\s*\{\s*float\s+value\s*=\s*values\[i\]' -Label "$Context payload value count"
    $result['bytes 1..14: scale/round'] = Get-RequiredCapture -Text $Text -Pattern 'scaled\s*=\s*\(uint16_t\)\(\s*(?<value>\(value\s*\*\s*100\.0f\)\s*\+\s*0\.5f)\s*\);' -Label "$Context payload scale"
    $result['bytes 1..14: low byte offsets'] = Get-RequiredCapture -Text $Text -Pattern 'payload\[\s*1U\s*\+\s*\(i\s*\*\s*2U\)\s*\]\s*=\s*(?<value>[^;]+);' -Label "$Context payload values low byte"
    $result['bytes 1..14: high byte offsets'] = Get-RequiredCapture -Text $Text -Pattern 'payload\[\s*2U\s*\+\s*\(i\s*\*\s*2U\)\s*\]\s*=\s*(?<value>[^;]+);' -Label "$Context payload values high byte"
    foreach ($offset in 15..23) {
        $result[('byte {0}' -f $offset)] = Get-RequiredCapture -Text $Text -Pattern ('payload\[\s*' + $offset + '(?:U)?\s*\]\s*=\s*(?<value>[^;]+);') -Label "$Context payload byte $offset"
    }
    $result['byte 23: validity mask update'] = Get-RequiredCapture -Text $Text -Pattern 'payload\[\s*23\s*\]\s*\|=\s*(?<value>[^;]+);' -Label "$Context temperature validity mask"
    $result['bytes 24,26,28,30: temperature low'] = Get-RequiredCapture -Text $Text -Pattern 'payload\[\s*24U\s*\+\s*\(i\s*\*\s*2U\)\s*\]\s*=\s*(?<value>[^;]+);' -Label "$Context temperature low byte"
    $result['bytes 25,27,29,31: temperature high'] = Get-RequiredCapture -Text $Text -Pattern 'payload\[\s*25U\s*\+\s*\(i\s*\*\s*2U\)\s*\]\s*=\s*(?<value>[^;]+);' -Label "$Context temperature high byte"
    $statusByte = Get-RequiredCapture -Text $Text -Pattern 'payload\[\s*32\s*\]\s*=\s*(?<value>[^;]+);' -Label "$Context reserved/status byte"
    if (($statusByte -eq '0U') -or ($statusByte -eq 'device_status_flags')) {
        $result['byte 32: reserved/status role'] = 'offset 32; allowed value is 0U or device_status_flags'
    }
    else {
        $result['byte 32: reserved/status role'] = "invalid expression: $statusByte"
        Add-Failure "$Context payload byte 32 may only be 0U or device_status_flags; found '$statusByte'"
    }
    $result['byte 32: actual value (allowed change)'] = $statusByte
    $result['wire endian'] = 'little-endian: low byte is written before high byte'

    $sendBlock = Get-CFunctionText -Text $Text -Name 'RAK_SendHexPayload' -Context $Context
    $appendMatches = [regex]::Matches($sendBlock,
        'TextAppend(?<integer>Int32)?\(cmd,\s*sizeof\(cmd\),\s*&pos,\s*(?<value>[^\)]+)\);')
    $appendSequence = New-Object 'System.Collections.Generic.List[string]'
    foreach ($appendMatch in $appendMatches) {
        $kind = $(if ($appendMatch.Groups['integer'].Success) { 'integer:' } else { 'text:' })
        $appendSequence.Add($kind + (Normalize-Scalar $appendMatch.Groups['value'].Value))
    }
    if ($appendSequence.Count -eq 0) {
        Add-Failure "Could not extract $Context AT+SEND framing sequence"
        $result['AT framing sequence'] = '<missing>'
    }
    else {
        $result['AT framing sequence'] = $appendSequence -join ' -> '
    }
    return $result
}

function Get-IrqMap {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Context
    )

    $specs = [ordered]@{
        'DMA2_Stream0_IRQHandler' = 'HAL_DMA_IRQHandler\((?<value>[^\)]+)\);'
        'TIM2_IRQHandler'         = 'HAL_TIM_IRQHandler\((?<value>[^\)]+)\);'
        'EXTI15_10_IRQHandler'    = 'HAL_GPIO_EXTI_IRQHandler\((?<value>[^\)]+)\);'
    }
    $result = [ordered]@{}
    foreach ($name in $specs.Keys) {
        $block = Get-CFunctionText -Text $Text -Name $name -Context $Context
        $result[$name] = Get-RequiredCapture -Text $block -Pattern $specs[$name] -Label "$Context::$name HAL target"
    }
    return $result
}

function Convert-CNumericLiteral {
    param(
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $clean = $Value.Trim()
    while (($clean.StartsWith('(')) -and ($clean.EndsWith(')'))) {
        $clean = $clean.Substring(1, $clean.Length - 2).Trim()
    }
    $clean = $clean -replace '[uUlLfF]+$', ''
    $number = 0.0
    if (-not [double]::TryParse($clean, [System.Globalization.NumberStyles]::Float,
            [System.Globalization.CultureInfo]::InvariantCulture, [ref]$number)) {
        Add-Failure "Could not convert $Label value '$Value' to a number"
        return [double]::NaN
    }
    return $number
}

function Convert-DividerToken {
    param(
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if ($Value -notmatch 'DIV(?<divider>[0-9]+)$') {
        Add-Failure "Could not extract divider from $Label value '$Value'"
        return [double]::NaN
    }
    return [double]$Matches.divider
}

function Get-TimingInfo {
    param(
        [Parameter(Mandatory = $true)][string]$MainText,
        [Parameter(Mandatory = $true)][string]$IocText,
        [Parameter(Mandatory = $true)][string]$Context
    )

    $timerBlock = Get-CFunctionText -Text $MainText -Name 'MX_TIM2_Init' -Context $Context
    $clockBlock = Get-CFunctionText -Text $MainText -Name 'SystemClock_Config' -Context $Context

    $hseText = Get-RequiredCapture -Text $IocText -Pattern '(?m)^RCC\.HSE_VALUE=(?<value>[0-9]+)\s*$' -Label "$Context HSE frequency"
    $pllMText = Get-RequiredCapture -Text $clockBlock -Pattern 'RCC_OscInitStruct\.PLL\.PLLM\s*=\s*(?<value>[^;]+);' -Label "$Context PLLM"
    $pllNText = Get-RequiredCapture -Text $clockBlock -Pattern 'RCC_OscInitStruct\.PLL\.PLLN\s*=\s*(?<value>[^;]+);' -Label "$Context PLLN"
    $pllPText = Get-RequiredCapture -Text $clockBlock -Pattern 'RCC_OscInitStruct\.PLL\.PLLP\s*=\s*(?<value>[^;]+);' -Label "$Context PLLP"
    $ahbText = Get-RequiredCapture -Text $clockBlock -Pattern 'RCC_ClkInitStruct\.AHBCLKDivider\s*=\s*(?<value>[^;]+);' -Label "$Context AHB divider"
    $apb1Text = Get-RequiredCapture -Text $clockBlock -Pattern 'RCC_ClkInitStruct\.APB1CLKDivider\s*=\s*(?<value>[^;]+);' -Label "$Context APB1 divider"
    $prescalerText = Get-RequiredCapture -Text $timerBlock -Pattern 'htim2\.Init\.Prescaler\s*=\s*(?<value>[^;]+);' -Label "$Context TIM2 prescaler"
    $periodText = Get-RequiredCapture -Text $timerBlock -Pattern 'htim2\.Init\.Period\s*=\s*(?<value>[^;]+);' -Label "$Context TIM2 period"
    $samplesText = Get-DefineValue -Text $MainText -Name 'RMS_SAMPLES' -Context $Context

    $hse = Convert-CNumericLiteral -Value $hseText -Label "$Context HSE"
    $pllM = Convert-CNumericLiteral -Value $pllMText -Label "$Context PLLM"
    $pllN = Convert-CNumericLiteral -Value $pllNText -Label "$Context PLLN"
    $pllP = Convert-DividerToken -Value $pllPText -Label "$Context PLLP"
    $ahbDivider = Convert-DividerToken -Value $ahbText -Label "$Context AHB divider"
    $apb1Divider = Convert-DividerToken -Value $apb1Text -Label "$Context APB1 divider"
    $prescaler = Convert-CNumericLiteral -Value $prescalerText -Label "$Context TIM2 prescaler"
    $period = Convert-CNumericLiteral -Value $periodText -Label "$Context TIM2 period"
    $samples = Convert-CNumericLiteral -Value $samplesText -Label "$Context RMS samples"

    $sysclk = ($hse / $pllM) * $pllN / $pllP
    $hclk = $sysclk / $ahbDivider
    $pclk1 = $hclk / $apb1Divider
    $timerClock = $pclk1
    if ($apb1Divider -gt 1.0) {
        $timerClock = $pclk1 * 2.0
    }
    $sampleRate = $timerClock / ($prescaler + 1.0) / ($period + 1.0)
    $windowSeconds = $samples / $sampleRate
    $cycles50Hz = $windowSeconds * 50.0

    return [pscustomobject]@{
        HseHz          = $hse
        SysclkHz       = $sysclk
        Tim2ClockHz    = $timerClock
        Prescaler      = $prescaler
        Period         = $period
        SampleRateHz   = $sampleRate
        RmsSamples     = $samples
        WindowSeconds  = $windowSeconds
        Cycles50Hz     = $cycles50Hz
        FullCycles50Hz = [math]::Floor($cycles50Hz + 1.0e-9)
    }
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$insideWorkTree = @(& git -c ("safe.directory={0}" -f $repositoryRoot) -C $repositoryRoot rev-parse --is-inside-work-tree 2>&1)
if (($LASTEXITCODE -ne 0) -or (($insideWorkTree -join '').Trim() -ne 'true')) {
    throw "$repositoryRoot is not a git working tree"
}

$headCommit = (@(& git -c ("safe.directory={0}" -f $repositoryRoot) -C $repositoryRoot rev-parse HEAD 2>&1) -join '').Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Could not resolve git HEAD'
}

$paths = [ordered]@{
    Main       = 'Core/Src/main.c'
    MainHeader = 'Core/Inc/main.h'
    Msp        = 'Core/Src/stm32f4xx_hal_msp.c'
    Interrupts = 'Core/Src/stm32f4xx_it.c'
    InterruptHeader = 'Core/Inc/stm32f4xx_it.h'
    Startup    = 'Core/Startup/startup_stm32f407vgtx.s'
    Ioc        = 'wind.ioc'
    Decoder    = 'chirpstack_decoder.js'
}

$baselineMain = Get-HeadFile -Repository $repositoryRoot -Path $paths.Main
$finalMain = Get-CurrentFile -Repository $repositoryRoot -Path $paths.Main
$baselineHeader = Get-HeadFile -Repository $repositoryRoot -Path $paths.MainHeader
$finalHeader = Get-CurrentFile -Repository $repositoryRoot -Path $paths.MainHeader
$baselineMsp = Get-HeadFile -Repository $repositoryRoot -Path $paths.Msp
$finalMsp = Get-CurrentFile -Repository $repositoryRoot -Path $paths.Msp
$baselineInterrupts = Get-HeadFile -Repository $repositoryRoot -Path $paths.Interrupts
$finalInterrupts = Get-CurrentFile -Repository $repositoryRoot -Path $paths.Interrupts
$headIocPaths = @(& git -c ("safe.directory={0}" -f $repositoryRoot) -C $repositoryRoot ls-tree -r --name-only HEAD 2>&1 |
    Where-Object { $_ -match '\.ioc$' })
if (($LASTEXITCODE -ne 0) -or ($headIocPaths.Count -ne 1)) {
    throw 'Expected exactly one .ioc file in git HEAD'
}
$baselineIoc = Get-HeadFile -Repository $repositoryRoot -Path $headIocPaths[0]
$finalIoc = Get-CurrentFile -Repository $repositoryRoot -Path $paths.Ioc
$baselineDecoder = Get-HeadFile -Repository $repositoryRoot -Path $paths.Decoder
$finalDecoder = Get-CurrentFile -Repository $repositoryRoot -Path $paths.Decoder

Write-Host ('Firmware invariant verification against HEAD {0}' -f $headCommit)
Write-Host ('Repository: {0}' -f $repositoryRoot)

$protectedFiles = @($paths.MainHeader, $paths.Decoder)
$protectedBaseline = [ordered]@{}
$protectedFinal = [ordered]@{}
foreach ($path in $protectedFiles) {
    $null = & git -c ("safe.directory={0}" -f $repositoryRoot) -C $repositoryRoot diff --quiet HEAD -- $path
    $diffExit = $LASTEXITCODE
    if ($diffExit -gt 1) {
        throw "git diff failed for $path"
    }
    $protectedBaseline[$path] = 'HEAD content'
    $protectedFinal[$path] = $(if ($diffExit -eq 0) { 'HEAD content' } else { 'CHANGED' })
}
Compare-Map -Title 'Protected files (must be byte-for-byte unchanged to git)' -Baseline $protectedBaseline -Final $protectedFinal

$iocExpected = [ordered]@{
    'ADC1.Channel-1\#ChannelRegularConversion' = 'ADC_CHANNEL_4'
    'ADC1.Channel-2\#ChannelRegularConversion' = 'ADC_CHANNEL_6'
    'ADC1.Channel-3\#ChannelRegularConversion' = 'ADC_CHANNEL_14'
    'ADC1.Channel-4\#ChannelRegularConversion' = 'ADC_CHANNEL_5'
    'ADC1.Channel-5\#ChannelRegularConversion' = 'ADC_CHANNEL_7'
    'ADC1.Channel-6\#ChannelRegularConversion' = 'ADC_CHANNEL_15'
    'ADC1.ClockPrescaler' = 'ADC_CLOCK_SYNC_PCLK_DIV4'
    'ADC1.ContinuousConvMode' = 'DISABLE'
    'ADC1.DMAContinuousRequests' = 'ENABLE'
    'ADC1.EOCSelection' = 'ADC_EOC_SEQ_CONV'
    'ADC1.ExternalTrigConv' = 'ADC_EXTERNALTRIGCONV_T2_TRGO'
    'ADC1.ExternalTrigConvEdge' = 'ADC_EXTERNALTRIGCONVEDGE_RISING'
    'Dma.ADC1.0.Instance' = 'DMA2_Stream0'
    'Dma.ADC1.0.MemInc' = 'DMA_MINC_ENABLE'
    'Dma.ADC1.0.PeriphDataAlignment' = 'DMA_PDATAALIGN_HALFWORD'
    'Dma.ADC1.0.MemDataAlignment' = 'DMA_MDATAALIGN_HALFWORD'
    'Dma.ADC1.0.Mode' = 'DMA_CIRCULAR'
    'PB12.Signal' = 'GPXTI12'
    'TIM2.Prescaler' = '8399'
    'TIM2.Period' = '3'
    'RCC.HSE_VALUE' = '8000000'
    'PA9.Signal' = 'USART1_TX'
    'PA10.Signal' = 'USART1_RX'
    'PD5.Signal' = 'USART2_TX'
    'PD6.Signal' = 'USART2_RX'
    'Mcu.Pin28' = 'VP_TIM2_VS_ClockSourceINT'
    'Mcu.Pin29' = 'PD5'
    'Mcu.Pin30' = 'PD6'
    'Mcu.PinsNb' = '31'
    'ProjectManager.ProjectFileName' = 'wind.ioc'
    'ProjectManager.ProjectName' = 'wind'
    'ProjectManager.StackSize' = '0x1000'
}
$iocActual = [ordered]@{}
for ($rank = 1; $rank -le 6; $rank++) {
    $iocExpected[('ADC1.Rank-{0}\#ChannelRegularConversion' -f $rank)] = [string]$rank
    $iocExpected[('ADC1.SamplingTime-{0}\#ChannelRegularConversion' -f $rank)] = 'ADC_SAMPLETIME_84CYCLES'
}
foreach ($name in $iocExpected.Keys) {
    $iocActual[$name] = Get-IocValue -Text $finalIoc -Name $name -Context 'working .ioc'
}
$iocSettings = @($finalIoc -split "`n" | Where-Object { $_ -match '^[^#;][^=]*=' })
$duplicateIocKeys = @($iocSettings | ForEach-Object { ($_ -split '=', 2)[0] } |
    Group-Object | Where-Object { $_.Count -gt 1 })
$duplicateIocPinValues = @($iocSettings | Where-Object { $_ -match '^Mcu\.Pin\d+=' } |
    ForEach-Object { ($_ -split '=', 2)[1] } | Group-Object | Where-Object { $_.Count -gt 1 })
if (($duplicateIocKeys.Count -ne 0) -or ($duplicateIocPinValues.Count -ne 0)) {
    Add-Failure 'working .ioc contains duplicate keys or duplicate Mcu.Pin values'
}
Compare-Map -Title 'CubeMX metadata matches the implemented hardware configuration' -Baseline $iocExpected -Final $iocActual

$measurementNames = @(
    'ADC_CHANNELS', 'ADC_MAX', 'VREF', 'ANALOG_MIDPOINT_V',
    'CURRENT_RSHUNT_OHM', 'CURRENT_GAIN_STAGE1', 'CURRENT_GAIN_TOTAL', 'CURRENT_V_PER_AMP',
    'VOLTAGE_R_TOP_OHM', 'VOLTAGE_R_BOTTOM_OHM', 'VOLTAGE_DIV_RATIO', 'VOLTAGE_GAIN_TOTAL',
    'BAT_DIV_R_TOP', 'BAT_DIV_R_BOTTOM', 'BAT_VOLTAGE_CORRECTION', 'BAT_LOW_THRESHOLD_V',
    'PULSES_PER_REV'
)
$baselineMeasurements = [ordered]@{}
$finalMeasurements = [ordered]@{}
foreach ($name in $measurementNames) {
    $baselineMeasurements[$name] = Get-DefineValue -Text $baselineMain -Name $name -Context 'HEAD main.c'
    $finalMeasurements[$name] = Get-DefineValue -Text $finalMain -Name $name -Context 'working main.c'
}
Compare-Map -Title 'Measurement, conversion, battery and rotation constants' -Baseline $baselineMeasurements -Final $finalMeasurements
Assert-ExactValue -Label 'VREF' -Actual $finalMeasurements.VREF -Expected '3.0f'
Assert-ExactValue -Label 'ANALOG_MIDPOINT_V' -Actual $finalMeasurements.ANALOG_MIDPOINT_V -Expected '1.5f'
Assert-ExactValue -Label 'ADC_MAX' -Actual $finalMeasurements.ADC_MAX -Expected '4095.0f'

$adcIndexNames = @('ADC_IDX_VA', 'ADC_IDX_VB', 'ADC_IDX_VC', 'ADC_IDX_IA', 'ADC_IDX_IB', 'ADC_IDX_IC')
$baselineAdcIndexes = [ordered]@{}
$finalAdcIndexes = [ordered]@{}
foreach ($name in $adcIndexNames) {
    $baselineAdcIndexes[$name] = Get-DefineValue -Text $baselineMain -Name $name -Context 'HEAD main.c'
    $finalAdcIndexes[$name] = Get-DefineValue -Text $finalMain -Name $name -Context 'working main.c'
}
Compare-Map -Title 'ADC DMA buffer index mapping' -Baseline $baselineAdcIndexes -Final $finalAdcIndexes
for ($index = 0; $index -lt $adcIndexNames.Count; $index++) {
    Assert-ExactValue -Label $adcIndexNames[$index] -Actual $finalAdcIndexes[$adcIndexNames[$index]] -Expected ('{0}U' -f $index)
}

$adc1Fields = @('Instance', 'Init.ClockPrescaler', 'Init.Resolution', 'Init.ScanConvMode',
    'Init.ContinuousConvMode', 'Init.DiscontinuousConvMode', 'Init.ExternalTrigConvEdge',
    'Init.ExternalTrigConv', 'Init.DataAlign', 'Init.NbrOfConversion',
    'Init.DMAContinuousRequests', 'Init.EOCSelection')
$adc2Fields = $adc1Fields
# Prefix duplicate ADC field names so both ADC instances remain visible.
$baselineAdcConfig = [ordered]@{}
$finalAdcConfig = [ordered]@{}
foreach ($adc in @(@('ADC1', 'MX_ADC1_Init', 'hadc1'), @('ADC2', 'MX_ADC2_Init', 'hadc2'))) {
    $baseFields = Get-AssignmentsMap -Text $baselineMain -FunctionName $adc[1] -ObjectName $adc[2] -Fields $adc1Fields -Context 'HEAD main.c'
    $workFields = Get-AssignmentsMap -Text $finalMain -FunctionName $adc[1] -ObjectName $adc[2] -Fields $adc1Fields -Context 'working main.c'
    foreach ($field in $adc1Fields) {
        $baselineAdcConfig[('{0}.{1}' -f $adc[0], $field)] = $baseFields[$field]
        $finalAdcConfig[('{0}.{1}' -f $adc[0], $field)] = $workFields[$field]
    }
}
Compare-Map -Title 'ADC instance and global configuration' -Baseline $baselineAdcConfig -Final $finalAdcConfig
Assert-ExactValue -Label 'ADC1 trigger source' -Actual $finalAdcConfig['ADC1.Init.ExternalTrigConv'] -Expected 'ADC_EXTERNALTRIGCONV_T2_TRGO'

$baselineAdc2Runtime = Get-AssignmentsMap -Text $baselineMain -FunctionName 'ADC2_ReadChannel' -ObjectName 'sConfig' -Fields @(
    'Channel', 'Rank', 'SamplingTime') -Context 'HEAD main.c'
$finalAdc2Runtime = Get-AssignmentsMap -Text $finalMain -FunctionName 'ADC2_ReadChannel' -ObjectName 'sConfig' -Fields @(
    'Channel', 'Rank', 'SamplingTime') -Context 'working main.c'
Compare-Map -Title 'ADC2 runtime channel/rank/sampling configuration' -Baseline $baselineAdc2Runtime -Final $finalAdc2Runtime

$baselineAdcSequence = Merge-Maps -Maps @(
    (Get-AdcSequenceMap -Text $baselineMain -FunctionName 'MX_ADC1_Init' -Context 'HEAD main.c'),
    (Get-AdcSequenceMap -Text $baselineMain -FunctionName 'MX_ADC2_Init' -Context 'HEAD main.c')
)
$finalAdcSequence = Merge-Maps -Maps @(
    (Get-AdcSequenceMap -Text $finalMain -FunctionName 'MX_ADC1_Init' -Context 'working main.c'),
    (Get-AdcSequenceMap -Text $finalMain -FunctionName 'MX_ADC2_Init' -Context 'working main.c')
)
# Rebuild with explicit ADC prefixes because rank 1 exists in both ADCs.
$baselineAdcSequence = [ordered]@{}
$finalAdcSequence = [ordered]@{}
foreach ($adc in @(@('ADC1', 'MX_ADC1_Init'), @('ADC2', 'MX_ADC2_Init'))) {
    $baseSequence = Get-AdcSequenceMap -Text $baselineMain -FunctionName $adc[1] -Context 'HEAD main.c'
    $workSequence = Get-AdcSequenceMap -Text $finalMain -FunctionName $adc[1] -Context 'working main.c'
    foreach ($key in $baseSequence.Keys) {
        $baselineAdcSequence[('{0} {1}' -f $adc[0], $key)] = $baseSequence[$key]
    }
    foreach ($key in $workSequence.Keys) {
        $finalAdcSequence[('{0} {1}' -f $adc[0], $key)] = $workSequence[$key]
    }
}
Compare-Map -Title 'ADC regular-channel rank, channel and sampling order' -Baseline $baselineAdcSequence -Final $finalAdcSequence

$baselineAux = Get-AuxAdcMap -Text $baselineMain -Context 'HEAD main.c'
$finalAux = Get-AuxAdcMap -Text $finalMain -Context 'working main.c'
Compare-Map -Title 'ADC2 runtime X/Y/Z/battery channel order' -Baseline $baselineAux -Final $finalAux

$baselineGpioDefinitions = Get-GpioDefinitionMap -Text $baselineHeader -Context 'HEAD main.h'
$finalGpioDefinitions = Get-GpioDefinitionMap -Text $finalHeader -Context 'working main.h'
Compare-Map -Title 'GPIO physical line definitions' -Baseline $baselineGpioDefinitions -Final $finalGpioDefinitions

$baselineGpioInit = Merge-Maps -Maps @(
    (Get-GpioInitMap -Text $baselineMain -FunctionName 'MX_GPIO_Init' -Context 'HEAD main.c' -SourceLabel 'main GPIO'),
    (Get-GpioInitMap -Text $baselineMain -FunctionName 'DEBUG_USART2_GPIO_Clock_Init' -Context 'HEAD main.c' -SourceLabel 'debug UART GPIO'),
    (Get-GpioInitMap -Text $baselineMsp -FunctionName 'HAL_ADC_MspInit' -Context 'HEAD MSP' -SourceLabel 'ADC MSP GPIO'),
    (Get-GpioInitMap -Text $baselineMsp -FunctionName 'HAL_UART_MspInit' -Context 'HEAD MSP' -SourceLabel 'RAK UART GPIO')
)
$finalGpioInit = Merge-Maps -Maps @(
    (Get-GpioInitMap -Text $finalMain -FunctionName 'MX_GPIO_Init' -Context 'working main.c' -SourceLabel 'main GPIO'),
    (Get-GpioInitMap -Text $finalMain -FunctionName 'DEBUG_USART2_GPIO_Clock_Init' -Context 'working main.c' -SourceLabel 'debug UART GPIO'),
    (Get-GpioInitMap -Text $finalMsp -FunctionName 'HAL_ADC_MspInit' -Context 'working MSP' -SourceLabel 'ADC MSP GPIO'),
    (Get-GpioInitMap -Text $finalMsp -FunctionName 'HAL_UART_MspInit' -Context 'working MSP' -SourceLabel 'RAK UART GPIO')
)
Compare-Map -Title 'GPIO modes, pulls, speeds and alternate functions' -Baseline $baselineGpioInit -Final $finalGpioInit `
    -AllowedDifferenceKeys @('ADC MSP GPIO #4')

$baselineMxGpio = Get-CFunctionText -Text $baselineMain -Name 'MX_GPIO_Init' -Context 'HEAD main.c'
$finalMxGpio = Get-CFunctionText -Text $finalMain -Name 'MX_GPIO_Init' -Context 'working main.c'
$baselineExtiConfig = [ordered]@{
    'EXTI15_10 priority' = Get-RequiredCapture -Text $baselineMxGpio -Pattern 'HAL_NVIC_SetPriority\(EXTI15_10_IRQn,\s*(?<value>[^\)]+)\);' -Label 'HEAD EXTI15_10 priority'
    'EXTI15_10 enable'   = Get-RequiredCapture -Text $baselineMxGpio -Pattern 'HAL_NVIC_EnableIRQ\((?<value>EXTI15_10_IRQn)\);' -Label 'HEAD EXTI15_10 enable'
}
$finalExtiConfig = [ordered]@{
    'EXTI15_10 priority' = Get-RequiredCapture -Text $finalMxGpio -Pattern 'HAL_NVIC_SetPriority\(EXTI15_10_IRQn,\s*(?<value>[^\)]+)\);' -Label 'working EXTI15_10 priority'
    'EXTI15_10 enable'   = Get-RequiredCapture -Text $finalMxGpio -Pattern 'HAL_NVIC_EnableIRQ\((?<value>EXTI15_10_IRQn)\);' -Label 'working EXTI15_10 enable'
}
Compare-Map -Title 'Rotation pulse EXTI line and priority' -Baseline $baselineExtiConfig -Final $finalExtiConfig

$dmaFields = @('Instance', 'Init.Channel', 'Init.Direction', 'Init.PeriphInc', 'Init.MemInc',
    'Init.PeriphDataAlignment', 'Init.MemDataAlignment', 'Init.Mode', 'Init.Priority', 'Init.FIFOMode')
$baselineDma = Get-AssignmentsMap -Text $baselineMsp -FunctionName 'HAL_ADC_MspInit' -ObjectName 'hdma_adc1' -Fields $dmaFields -Context 'HEAD MSP'
$finalDma = Get-AssignmentsMap -Text $finalMsp -FunctionName 'HAL_ADC_MspInit' -ObjectName 'hdma_adc1' -Fields $dmaFields -Context 'working MSP'
Compare-Map -Title 'ADC1 DMA stream/channel/direction/alignment/mode' -Baseline $baselineDma -Final $finalDma
Assert-ExactValue -Label 'ADC1 DMA stream' -Actual $finalDma.Instance -Expected 'DMA2_Stream0'
Assert-ExactValue -Label 'ADC1 DMA channel' -Actual $finalDma['Init.Channel'] -Expected 'DMA_CHANNEL_0'
Assert-ExactValue -Label 'ADC1 DMA mode' -Actual $finalDma['Init.Mode'] -Expected 'DMA_CIRCULAR'

$uartFields = @('Instance', 'Init.BaudRate', 'Init.WordLength', 'Init.StopBits', 'Init.Parity',
    'Init.Mode', 'Init.HwFlowCtl', 'Init.OverSampling')
$baselineUart = [ordered]@{
    'RAK_UART macro'   = Get-DefineValue -Text $baselineMain -Name 'RAK_UART' -Context 'HEAD main.c'
    'DEBUG_UART macro' = Get-DefineValue -Text $baselineMain -Name 'DEBUG_UART' -Context 'HEAD main.c'
}
$finalUart = [ordered]@{
    'RAK_UART macro'   = Get-DefineValue -Text $finalMain -Name 'RAK_UART' -Context 'working main.c'
    'DEBUG_UART macro' = Get-DefineValue -Text $finalMain -Name 'DEBUG_UART' -Context 'working main.c'
}
foreach ($uart in @(@('USART1/RAK', 'MX_USART1_UART_Init', 'huart1'), @('USART2/debug', 'MX_USART2_UART_Init', 'huart2'))) {
    $baseFields = Get-AssignmentsMap -Text $baselineMain -FunctionName $uart[1] -ObjectName $uart[2] -Fields $uartFields -Context 'HEAD main.c'
    $workFields = Get-AssignmentsMap -Text $finalMain -FunctionName $uart[1] -ObjectName $uart[2] -Fields $uartFields -Context 'working main.c'
    foreach ($field in $uartFields) {
        $baselineUart[('{0}.{1}' -f $uart[0], $field)] = $baseFields[$field]
        $finalUart[('{0}.{1}' -f $uart[0], $field)] = $workFields[$field]
    }
}
Compare-Map -Title 'UART ownership, instances and serial format' -Baseline $baselineUart -Final $finalUart
Assert-ExactValue -Label 'RAK UART mapping' -Actual $finalUart['RAK_UART macro'] -Expected 'huart1'
Assert-ExactValue -Label 'Debug UART mapping' -Actual $finalUart['DEBUG_UART macro'] -Expected 'huart2'
Assert-ExactValue -Label 'RAK UART instance' -Actual $finalUart['USART1/RAK.Instance'] -Expected 'USART1'
Assert-ExactValue -Label 'RAK UART baud' -Actual $finalUart['USART1/RAK.Init.BaudRate'] -Expected '115200'
Assert-ExactValue -Label 'Debug UART instance' -Actual $finalUart['USART2/debug.Instance'] -Expected 'USART2'
Assert-ExactValue -Label 'Debug UART baud' -Actual $finalUart['USART2/debug.Init.BaudRate'] -Expected '115200'

$baselineTimerInvariant = Get-AssignmentsMap -Text $baselineMain -FunctionName 'MX_TIM2_Init' -ObjectName 'htim2' -Fields @(
    'Instance', 'Init.CounterMode', 'Init.ClockDivision', 'Init.AutoReloadPreload') -Context 'HEAD main.c'
$finalTimerInvariant = Get-AssignmentsMap -Text $finalMain -FunctionName 'MX_TIM2_Init' -ObjectName 'htim2' -Fields @(
    'Instance', 'Init.CounterMode', 'Init.ClockDivision', 'Init.AutoReloadPreload') -Context 'working main.c'
$timerPatterns = [ordered]@{
    'ClockSource'         = 'sClockSourceConfig\.ClockSource\s*=\s*(?<value>[^;]+);'
    'MasterOutputTrigger' = 'sMasterConfig\.MasterOutputTrigger\s*=\s*(?<value>[^;]+);'
    'MasterSlaveMode'     = 'sMasterConfig\.MasterSlaveMode\s*=\s*(?<value>[^;]+);'
    'IRQ priority'        = 'HAL_NVIC_SetPriority\(TIM2_IRQn,\s*(?<value>[^\)]+)\);'
    'IRQ enable'          = 'HAL_NVIC_EnableIRQ\((?<value>TIM2_IRQn)\);'
}
$baselineTimerBlock = Get-CFunctionText -Text $baselineMain -Name 'MX_TIM2_Init' -Context 'HEAD main.c'
$finalTimerBlock = Get-CFunctionText -Text $finalMain -Name 'MX_TIM2_Init' -Context 'working main.c'
foreach ($label in $timerPatterns.Keys) {
    $baselineTimerInvariant[$label] = Get-RequiredCapture -Text $baselineTimerBlock -Pattern $timerPatterns[$label] -Label "HEAD TIM2 $label"
    $finalTimerInvariant[$label] = Get-RequiredCapture -Text $finalTimerBlock -Pattern $timerPatterns[$label] -Label "working TIM2 $label"
}
Compare-Map -Title 'TIM2 resource/source invariants (PSC and ARR intentionally excluded)' -Baseline $baselineTimerInvariant -Final $finalTimerInvariant
Assert-ExactValue -Label 'ADC trigger timer instance' -Actual $finalTimerInvariant.Instance -Expected 'TIM2'
Assert-ExactValue -Label 'ADC trigger TRGO' -Actual $finalTimerInvariant.MasterOutputTrigger -Expected 'TIM_TRGO_UPDATE'

$baselineFormula = Get-RmsSemanticMap -Text $baselineMain -Context 'HEAD main.c'
$finalFormula = Get-RmsSemanticMap -Text $finalMain -Context 'working main.c'
Compare-Map -Title 'RMS, mean/variance, scaling, accumulation and phase semantics' -Baseline $baselineFormula -Final $finalFormula

$baselineDmaShape = Get-DmaShapeMap -Text $baselineMain -Context 'HEAD main.c'
$finalDmaShape = Get-DmaShapeMap -Text $finalMain -Context 'working main.c'
Compare-Map -Title 'ADC DMA acquisition shape (larger whole-sequence circular buffer is allowed)' -Baseline $baselineDmaShape -Final $finalDmaShape -FailOnDifference $false

$baselinePacket = Get-PacketMap -Text $baselineMain -Context 'HEAD main.c'
$finalPacket = Get-PacketMap -Text $finalMain -Context 'working main.c'
Compare-Map -Title 'LoRaWAN/RAK binary payload size, offsets, endian, scales and fPort' -Baseline $baselinePacket -Final $finalPacket `
    -AllowedDifferenceKeys @('byte 32: actual value (allowed change)')
Assert-ExactValue -Label 'RAK payload size' -Actual $finalPacket['payload size'] -Expected '33U'
Assert-ExactValue -Label 'RAK fPort' -Actual $finalPacket.fPort -Expected '2'
Assert-ExactValue -Label 'RAK payload version' -Actual $finalPacket['byte 0: format version'] -Expected '2U'

$baselineIrq = Get-IrqMap -Text $baselineInterrupts -Context 'HEAD stm32f4xx_it.c'
$finalIrq = Get-IrqMap -Text $finalInterrupts -Context 'working stm32f4xx_it.c'
Compare-Map -Title 'Existing IRQ line-to-HAL-handle mappings (additional IRQ code is allowed)' -Baseline $baselineIrq -Final $finalIrq

$startupText = Get-CurrentFile -Repository $repositoryRoot -Path $paths.Startup
$interruptHeaderText = Get-CurrentFile -Repository $repositoryRoot -Path $paths.InterruptHeader
$rtcHandler = Get-CFunctionText -Text $finalInterrupts -Name 'RTC_WKUP_IRQHandler' -Context 'working stm32f4xx_it.c'
$rtcAppHandler = Get-CFunctionText -Text $finalMain -Name 'App_RTCWakeupIRQ' -Context 'working main.c'
$rtcStart = Get-CFunctionText -Text $finalMain -Name 'RTC_WakeupStart' -Context 'working main.c'
$stopCycle = Get-CFunctionText -Text $finalMain -Name 'App_EnterStopCycle' -Context 'working main.c'
$rotationMask = Get-CFunctionText -Text $finalMain -Name 'Rotation_SetExtiEnabled' -Context 'working main.c'
$joinHandler = Get-CFunctionText -Text $finalMain -Name 'RAK_HandleJoin' -Context 'working main.c'
$uplinkFailureHandler = Get-CFunctionText -Text $finalMain -Name 'RAK_CompleteUplinkFailure' -Context 'working main.c'
$batteryCompletionHandler = Get-CFunctionText -Text $finalMain -Name 'Battery_HandleCompleted' -Context 'working main.c'
$allCoreC = ((Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'Core\Src') -Filter '*.c' -File | ForEach-Object {
    Get-Content -LiteralPath $_.FullName -Raw
}) -join "`n")
$cmakeLists = Get-Content -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt') -Raw
$cmakePresets = Get-Content -LiteralPath (Join-Path $repositoryRoot 'CMakePresets.json') -Raw
$cmakeToolchain = Get-Content -LiteralPath (Join-Path $repositoryRoot 'cmake\arm-none-eabi-gcc.cmake') -Raw
$ciWorkflow = Get-Content -LiteralPath (Join-Path $repositoryRoot '.github\workflows\firmware-ci.yml') -Raw
$eclipseProject = Get-Content -LiteralPath (Join-Path $repositoryRoot '.project') -Raw
$debugLaunch = Get-Content -LiteralPath (Join-Path $repositoryRoot 'wind-debug.launch') -Raw
$flashLinker = Get-Content -LiteralPath (Join-Path $repositoryRoot 'STM32F407VGTX_FLASH.ld') -Raw
$ramLinker = Get-Content -LiteralPath (Join-Path $repositoryRoot 'STM32F407VGTX_RAM.ld') -Raw

$integrationChecks = [ordered]@{
    'startup vector references RTC_WKUP_IRQHandler' = [regex]::IsMatch($startupText, '(?m)^\s*\.word\s+RTC_WKUP_IRQHandler\b')
    'exactly one RTC_WKUP_IRQHandler definition' = ([regex]::Matches($allCoreC, '(?m)^\s*void\s+RTC_WKUP_IRQHandler\s*\(').Count -eq 1)
    'RTC handler prototype is exported' = [regex]::IsMatch($interruptHeaderText, '(?m)^\s*void\s+RTC_WKUP_IRQHandler\s*\(void\)\s*;')
    'RTC IRQ delegates only to App_RTCWakeupIRQ' = [regex]::IsMatch($rtcHandler, 'App_RTCWakeupIRQ\s*\(\s*\)\s*;') -and (-not [regex]::IsMatch($rtcHandler, 'Debug_Log|HAL_UART|HAL_Delay'))
    'RTC IRQ clears WUTF and EXTI22' = [regex]::IsMatch($rtcAppHandler, 'RTC->ISR\s*&=\s*~RTC_ISR_WUTF') -and [regex]::IsMatch($rtcAppHandler, 'EXTI->PR\s*=\s*EXTI_PR_PR22')
    'RTC flags are cleared before WUT enable' = [regex]::IsMatch($rtcStart, 'RTC->ISR\s*&=\s*~RTC_ISR_WUTF.*?EXTI->PR\s*=\s*EXTI_PR_PR22.*?HAL_NVIC_ClearPendingIRQ\(RTC_WKUP_IRQn\).*?RTC->CR\s*\|=\s*RTC_CR_WUTIE\s*\|\s*RTC_CR_WUTE', 'Singleline')
    'RTC NVIC is enabled' = [regex]::IsMatch($finalMain, 'HAL_NVIC_EnableIRQ\(RTC_WKUP_IRQn\)')
    'STOP recovery rebuilds temporary and final SysTick before DWT' = [regex]::IsMatch($stopCycle, 'SystemCoreClockUpdate\(\).*?HAL_InitTick\(TICK_INT_PRIORITY\).*?HAL_ResumeTick\(\).*?SystemClock_Config\(\).*?SystemCoreClockUpdate\(\).*?HAL_InitTick\(TICK_INT_PRIORITY\).*?HAL_ResumeTick\(\).*?DWT_Delay_Init\(\)', 'Singleline')
    'DMA2 Stream0 IRQ delegates to hdma_adc1' = [regex]::IsMatch($finalInterrupts, 'DMA2_Stream0_IRQHandler\s*\(void\).*?HAL_DMA_IRQHandler\(&hdma_adc1\)', 'Singleline')
    'ADC half/full callbacks are both present' = [regex]::IsMatch($finalMain, 'HAL_ADC_ConvHalfCpltCallback') -and [regex]::IsMatch($finalMain, 'HAL_ADC_ConvCpltCallback')
    'shared EXTI15_10 NVIC is never disabled' = -not [regex]::IsMatch($finalMain, 'HAL_NVIC_DisableIRQ\(EXTI15_10_IRQn\)')
    'rotation masks only Freqency_Pin in EXTI IMR' = [regex]::IsMatch($rotationMask, 'AppLogic_ExtiMaskUpdate\(EXTI->IMR,\s*\(uint32_t\)Freqency_Pin,\s*(?:true|false)\)', 'Singleline')
    'JOIN uses max RAK attempts and an unlimited STM32 retry loop' = [regex]::IsMatch($finalMain, 'AT\+JOIN=1:1:10:255\\r\\n') -and ((Get-DefineValue -Text $finalMain -Name 'RAK_JOIN_RETRY_DELAY_MS' -Context 'working main.c') -eq '10000U')
    'only the exact JOINED helper gates ACTIVE mode' = [regex]::IsMatch($joinHandler, 'if\s*\(AppLogic_JoinConfirmed\(events\)\)')
    'RAK receives a full power cycle after an uplink failure' = [regex]::IsMatch($uplinkFailureHandler, 'App_RequestReconnect\(now,\s*reason\)') -and (-not [regex]::IsMatch($uplinkFailureHandler, 'UPLINK_STATE_RETRY_WAIT'))
    'RAK boot and power-off waits are long enough for a real restart' = ((Get-DefineValue -Text $finalMain -Name 'RAK_BOOT_DELAY_MS' -Context 'working main.c') -eq '5000U') -and ((Get-DefineValue -Text $finalMain -Name 'RAK_POWER_OFF_DELAY_MS' -Context 'working main.c') -eq '1000U')
    'battery check remains every ten completed JOIN failures' = ((Get-DefineValue -Text $finalMain -Name 'RAK_JOIN_FAILURES_PER_BATTERY_CHECK' -Context 'working main.c') -eq '10U')
    'requested uplink period and BUSY retry are five seconds' = ((Get-DefineValue -Text $finalMain -Name 'UPLINK_PERIOD_MS' -Context 'working main.c') -eq '5000U') -and ((Get-DefineValue -Text $finalMain -Name 'UPLINK_BUSY_RETRY_MS' -Context 'working main.c') -eq '5000U')
    'first uplink is armed immediately after measurements start' = [regex]::IsMatch($finalMain, 'DS18B20_DiscoverCached\(now\).*?next_uplink_tick\s*=\s*now\s*;', 'Singleline')
    'shutdown requires both low battery and confirmed zero rotation' = [regex]::IsMatch($batteryCompletionHandler, 'BATTERY_REASON_ROTATION_STOP.*?AppLogic_ShouldShutdown\(true,\s*battery_voltage,\s*BAT_LOW_THRESHOLD_V,\s*true,\s*new_pulses\).*?App_EnterEmergency', 'Singleline')
    'low battery alone has no direct emergency path' = (-not [regex]::IsMatch($finalMain, 'verified battery below 3\.6 V|verified low battery during uplink preparation'))
    'UART ring power-of-two static assertion is present' = [regex]::IsMatch($finalMain, '_Static_assert\s*\(\s*\(RAK_RX_RING_SIZE\s*&\s*\(RAK_RX_RING_SIZE\s*-\s*1U\)\)\s*==\s*0U')
    'PWR_OFF emergency sequence remains high/low one second' = [regex]::IsMatch($finalMain, 'HAL_GPIO_WritePin\(PWR_OFF_GPIO_Port,\s*PWR_OFF_Pin,\s*GPIO_PIN_SET\).*?HAL_Delay\(1000U\).*?HAL_GPIO_WritePin\(PWR_OFF_GPIO_Port,\s*PWR_OFF_Pin,\s*GPIO_PIN_RESET\).*?HAL_Delay\(1000U\)', 'Singleline')
    'project identity is consistently wind' = [regex]::IsMatch($eclipseProject, '<name>wind</name>') -and [regex]::IsMatch($finalIoc, 'ProjectManager\.ProjectName=wind') -and [regex]::IsMatch($debugLaunch, 'Debug/wind\.elf')
    'CMake builds wind ELF, HEX and BIN with the project linker script' = [regex]::IsMatch($cmakeLists, 'project\(wind\s+LANGUAGES\s+C\s+ASM\)') -and [regex]::IsMatch($cmakeLists, '-T\$\{CMAKE_SOURCE_DIR\}/STM32F407VGTX_FLASH\.ld') -and [regex]::IsMatch($cmakeLists, 'wind\.hex') -and [regex]::IsMatch($cmakeLists, 'wind\.bin')
    'CMake presets provide warning-clean Debug and Release builds' = [regex]::IsMatch($cmakePresets, '"name"\s*:\s*"debug"') -and [regex]::IsMatch($cmakePresets, '"name"\s*:\s*"release"') -and [regex]::IsMatch($cmakePresets, '"WIND_WARNINGS_AS_ERRORS"\s*:\s*"ON"')
    'official ARM GCC toolchain keeps stack usage reports' = [regex]::IsMatch($cmakeToolchain, 'arm-none-eabi-gcc') -and [regex]::IsMatch($cmakeLists, '-fstack-usage') -and (-not [regex]::IsMatch($cmakeLists, '-fcyclomatic-complexity'))
    'CI runs scenarios, invariants and the release build' = [regex]::IsMatch($ciWorkflow, 'Run host scenarios') -and [regex]::IsMatch($ciWorkflow, 'verify_firmware_invariants\.ps1') -and [regex]::IsMatch($ciWorkflow, 'cmake --build --preset release')
    'both linker scripts reserve at least four KiB for stack' = [regex]::IsMatch($flashLinker, '_Min_Stack_Size\s*=\s*0x1000') -and [regex]::IsMatch($ramLinker, '_Min_Stack_Size\s*=\s*0x1000')
}
$integrationRows = @()
foreach ($label in $integrationChecks.Keys) {
    $passed = [bool]$integrationChecks[$label]
    if (-not $passed) {
        Add-Failure "Required integration check failed: $label"
    }
    $integrationRows += [pscustomobject]@{ Check = $label; Status = $(if ($passed) { 'PASS' } else { 'FAIL' }) }
}
Write-Section 'Required RTC, DMA, EXTI, JOIN, UART, power and build integration'
Show-Table $integrationRows

$functionInvariants = @(
    'SystemClock_Config', 'MX_ADC1_Init', 'MX_ADC2_Init',
    'MX_USART1_UART_Init', 'MX_USART2_UART_Init', 'MX_DMA_Init',
    'DEBUG_USART2_GPIO_Clock_Init', 'RawToVoltage', 'BatteryVoltageFromRaw',
    'RAK_BytesToHex'
)
$baselineFunctionHashes = [ordered]@{}
$finalFunctionHashes = [ordered]@{}
foreach ($name in $functionInvariants) {
    $baselineFunctionHashes[$name] = Get-TextFingerprint (Get-CFunctionText -Text $baselineMain -Name $name -Context 'HEAD main.c')
    $finalFunctionHashes[$name] = Get-TextFingerprint (Get-CFunctionText -Text $finalMain -Name $name -Context 'working main.c')
}
Compare-Map -Title 'Protected main.c function fingerprints' -Baseline $baselineFunctionHashes -Final $finalFunctionHashes

$baselineTiming = Get-TimingInfo -MainText $baselineMain -IocText $baselineIoc -Context 'HEAD'
$finalTiming = Get-TimingInfo -MainText $finalMain -IocText $finalIoc -Context 'working tree'
$timingRows = @(
    [pscustomobject]@{ Metric = 'HSE (Hz)'; Baseline = ('{0:F0}' -f $baselineTiming.HseHz); Final = ('{0:F0}' -f $finalTiming.HseHz) },
    [pscustomobject]@{ Metric = 'SYSCLK (Hz)'; Baseline = ('{0:F0}' -f $baselineTiming.SysclkHz); Final = ('{0:F0}' -f $finalTiming.SysclkHz) },
    [pscustomobject]@{ Metric = 'TIM2 clock (Hz)'; Baseline = ('{0:F0}' -f $baselineTiming.Tim2ClockHz); Final = ('{0:F0}' -f $finalTiming.Tim2ClockHz) },
    [pscustomobject]@{ Metric = 'TIM2 PSC register'; Baseline = ('{0:F0}' -f $baselineTiming.Prescaler); Final = ('{0:F0}' -f $finalTiming.Prescaler) },
    [pscustomobject]@{ Metric = 'TIM2 ARR register'; Baseline = ('{0:F0}' -f $baselineTiming.Period); Final = ('{0:F0}' -f $finalTiming.Period) },
    [pscustomobject]@{ Metric = 'ADC sequence rate Fs (Hz)'; Baseline = ('{0:F6}' -f $baselineTiming.SampleRateHz); Final = ('{0:F6}' -f $finalTiming.SampleRateHz) },
    [pscustomobject]@{ Metric = 'RMS samples/channel'; Baseline = ('{0:F0}' -f $baselineTiming.RmsSamples); Final = ('{0:F0}' -f $finalTiming.RmsSamples) },
    [pscustomobject]@{ Metric = 'RMS window (s)'; Baseline = ('{0:F6}' -f $baselineTiming.WindowSeconds); Final = ('{0:F6}' -f $finalTiming.WindowSeconds) },
    [pscustomobject]@{ Metric = '50 Hz periods in window'; Baseline = ('{0:F6}' -f $baselineTiming.Cycles50Hz); Final = ('{0:F6}' -f $finalTiming.Cycles50Hz) },
    [pscustomobject]@{ Metric = 'complete 50 Hz periods'; Baseline = ('{0:F0}' -f $baselineTiming.FullCycles50Hz); Final = ('{0:F0}' -f $finalTiming.FullCycles50Hz) }
)
Write-Section 'ADC sampling frequency and RMS window (timing changes allowed)'
Show-Table $timingRows

$timingChanged = (($baselineTiming.Prescaler -ne $finalTiming.Prescaler) -or
    ($baselineTiming.Period -ne $finalTiming.Period) -or
    ($baselineTiming.RmsSamples -ne $finalTiming.RmsSamples))
if ($finalTiming.FullCycles50Hz -lt $baselineTiming.FullCycles50Hz) {
    Add-Failure ('Final RMS window contains fewer complete 50 Hz periods ({0}) than HEAD ({1})' -f
        $finalTiming.FullCycles50Hz, $baselineTiming.FullCycles50Hz)
}
if ($timingChanged) {
    if ($finalTiming.SampleRateHz -le 100.0) {
        Add-Failure ('Final ADC sequence rate {0:F6} Hz still violates the strict Nyquist condition Fs > 100 Hz for 50 Hz' -f $finalTiming.SampleRateHz)
    }
}
elseif ($finalTiming.SampleRateHz -le 100.0) {
    Add-Warning ('Timing has not yet changed; current Fs={0:F6} Hz violates Fs > 100 Hz for a 50 Hz signal' -f $finalTiming.SampleRateHz)
}

Write-Section 'Result'
if ($script:Warnings.Count -gt 0) {
    Write-Host 'Warnings:'
    foreach ($warning in $script:Warnings) {
        Write-Host ('  - {0}' -f $warning)
    }
}

if ($script:Failures.Count -gt 0) {
    Write-Host ('FAIL: {0} invariant verification error(s)' -f $script:Failures.Count)
    foreach ($failure in $script:Failures) {
        Write-Host ('  - {0}' -f $failure)
    }
    exit 1
}

Write-Host 'PASS: firmware hardware, measurement, packet and production-build invariants are satisfied.'
exit 0
