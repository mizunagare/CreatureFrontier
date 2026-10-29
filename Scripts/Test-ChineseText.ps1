param([string]$SourceRoot=(Join-Path (Split-Path $PSScriptRoot -Parent) 'Source\CreatureFrontier'),
      [string]$PythonRoot=(Join-Path (Split-Path $PSScriptRoot -Parent) 'Content\Python'))
$ErrorActionPreference='Stop'
if(!(Test-Path -LiteralPath $SourceRoot -PathType Container)){throw '找不到待检查的源码目录'}
if(!(Test-Path -LiteralPath $PythonRoot -PathType Container)){throw '找不到待检查的资产脚本目录'}

# 只读补充检查；资产另用audit_chinese.py核对，截图仍须人工检查字体与排版。
# 按键标识和字符编码名称属于技术文字，不强行翻译，也不改玩家昵称。
$allowed=@('WASD','Shift','Tab','Insert','UTF-16')
$failures=@()
$checked=0
foreach($file in Get-ChildItem -LiteralPath $SourceRoot -File -Recurse | Where-Object Extension -In '.cpp','.h'){
    $lineNumber=0
    foreach($line in Get-Content -LiteralPath $file.FullName -Encoding UTF8){
        $lineNumber++
        foreach($literal in [regex]::Matches($line,'TEXT\("((?:\\.|[^"\\])*)"\)')){
            $value=$literal.Groups[1].Value
            if($value -match '[\u4e00-\u9fff]'){
                $checked++
                $words=$value -replace '\\[nrt]',' ' -replace '%[-+0 #]*\d*(?:\.\d+)?[sdfui]',' '
                foreach($token in [regex]::Matches($words,'[A-Za-z][A-Za-z0-9_-]*')){
                    $word=$token.Value
                    if($word.Length -gt 1 -and $word -notmatch '^F\d{1,2}$' -and $word -cnotin $allowed){
                        $failures+=('{0}:{1} 展示文字中有未核准英文：{2}' -f $file.FullName,$lineNumber,$word)
                    }
                }
            }elseif($file.Name -notmatch 'Scenario|Preview|HollowAdventure' -and
                $line -match 'Feedback\s*=|DrawText\(|FText::FromString\(\s*TEXT\(' -and
                $value -match '[A-Za-z]{2,}' -and $value -notmatch '^CF_'){
                $failures+=('{0}:{1} 直接展示文字缺少中文：{2}' -f $file.FullName,$lineNumber,$value)
            }
        }
    }
}
# 资产生成脚本的说明也属于开发文案；纯协议标记及键值字段原样保留。
# 这里只检查同一行内的日志字符串，不执行任何可能重建地图或覆盖资产的脚本。
$checkedTools=0
foreach($file in Get-ChildItem -LiteralPath $PythonRoot -File -Recurse -Filter '*.py'){
    $lineNumber=0
    foreach($line in Get-Content -LiteralPath $file.FullName -Encoding UTF8){
        $lineNumber++
        foreach($literal in [regex]::Matches($line,'unreal\.(?:log|log_warning|log_error)\(\s*[fr]?(?<quote>[''"])(?<value>.*?)\k<quote>')){
            $value=$literal.Groups['value'].Value
            $checkedTools++
            if($value -match '[\u4e00-\u9fff]'){
                $explanation=$value -creplace '^[A-Z][A-Z0-9_]*(?:\s+(?:PASS|FAIL))?','' -replace '\{[^{}]*\}','' -replace '%[-+0 #]*\d*(?:\.\d+)?[sdfui]','' -replace '\b[A-Za-z_][A-Za-z0-9_]*=\S+',''
                if($explanation -match '[A-Za-z]{2,}'){
                    $failures+=('{0}:{1} 开发提示中残留英文说明：{2}' -f $file.FullName,$lineNumber,$value)
                }
            }elseif(
               $value -cnotmatch '^[A-Z][A-Z0-9_]*(?:\s+(?:PASS|FAIL))?(?:\s+[A-Za-z_][A-Za-z0-9_]*=\S+)*\s*$'){
                $failures+=('{0}:{1} 开发提示缺少中文：{2}' -f $file.FullName,$lineNumber,$value)
            }
        }
    }
}
if($failures.Count){$failures | ForEach-Object {Write-Output $_};throw ('中文源码检查失败，共'+$failures.Count+'处')}
if(!$checked){throw '没有找到中文展示字符串，不能视作检查通过'}
if(!$checkedTools){throw '没有找到开发脚本日志，不能视作检查通过'}
Write-Host ('中文源码检查通过：'+$checked+'条含中文的文字及'+$checkedTools+'条开发脚本日志；直接展示入口未发现纯英文文案。此静态抽样不替代资产、动态拼接或图形验收。')
