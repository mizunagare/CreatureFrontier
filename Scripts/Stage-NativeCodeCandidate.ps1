param(
    [Parameter(Mandatory=$true)][string]$SourceBuildRoot,
    [Parameter(Mandatory=$true)][string]$SourceProgramSHA256,
    [Parameter(Mandatory=$true)][string]$AssetBaselinePath,
    [Parameter(Mandatory=$true)][string]$Destination,
    [switch]$ConfirmNativeCodeOnly
)
$ErrorActionPreference='Stop'
# 仅用于人工确认未改反射结构、地图或资产的本地原生代码更新；不是通用烹饪替代品。
if(!$ConfirmNativeCodeOnly){throw '必须先确认本轮仅更新原生代码，没有反射结构或烹饪资产变化'}
$projectRoot=Split-Path $PSScriptRoot -Parent
$SourceBuildRoot=(Resolve-Path -LiteralPath $SourceBuildRoot).Path.TrimEnd('\','/')
$Destination=[IO.Path]::GetFullPath($Destination).TrimEnd('\','/')
if(!(Test-Path -LiteralPath $AssetBaselinePath -PathType Leaf)){throw '找不到与已验收来源构建对应的源资产清单'}
if(Test-Path -LiteralPath $Destination){throw '候选目录已存在，停止以免覆盖已有构建'}
if([IO.Path]::GetPathRoot($SourceBuildRoot) -ne [IO.Path]::GetPathRoot($Destination)){throw '硬链接候选必须与来源构建位于同一卷'}
if($Destination.StartsWith($SourceBuildRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or $SourceBuildRoot.StartsWith($Destination+'\',[StringComparison]::OrdinalIgnoreCase)){throw '来源和目标构建目录不能互相包含'}
$relativeProgram='CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
$sourceProgram=Join-Path $SourceBuildRoot $relativeProgram
$newProgram=Join-Path $projectRoot 'Binaries\Win64\CreatureFrontier.exe'
if(!(Test-Path -LiteralPath $newProgram) -or !(Test-Path -LiteralPath (Join-Path $SourceBuildRoot 'CreatureFrontier.exe'))){throw '缺少新编译程序或已验收来源入口'}
if((Get-FileHash -LiteralPath $sourceProgram).Hash -ne $SourceProgramSHA256){throw '来源程序指纹与指定已验收构建不一致'}
$newHash=(Get-FileHash -LiteralPath $newProgram).Hash
$baseline=@(Import-Csv -LiteralPath $AssetBaselinePath)
$assets=@(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Content') -File -Recurse | Where-Object Extension -In '.uasset','.umap')
if(!$baseline.Count -or $baseline.Count -ne $assets.Count){throw '源资产清单数量变化，必须重新烹饪'}
$known=@{}
foreach($entry in $baseline){
    $path=[IO.Path]::GetFullPath($entry.Path)
    if(!$path.StartsWith($projectRoot+'\Content\',[StringComparison]::OrdinalIgnoreCase) -or $known.ContainsKey($path) -or (Get-FileHash -LiteralPath $path).Hash -ne $entry.Hash){throw '源资产清单无效或内容变化，必须重新烹饪'}
    $known[$path]=$entry.Hash
}
foreach($asset in $assets){if(!$known.ContainsKey($asset.FullName)){throw '发现新源资产，必须重新烹饪'}}
$files=@(Get-ChildItem -LiteralPath $SourceBuildRoot -File -Recurse | Where-Object {
    $_.FullName -notmatch '[\\/](Saved|Intermediate|DerivedDataCache)[\\/]' -and $_.Extension -notin '.pdb','.sav','.log'
})
New-Item -ItemType Directory -Path $Destination | Out-Null
$manifest=@();$linked=0;$copied=0
foreach($file in $files){
    $relative=[IO.Path]::GetRelativePath($SourceBuildRoot,$file.FullName)
    if($relative -eq $relativeProgram -or $relative -eq 'StageEvidence.csv'){continue}
    $target=Join-Path $Destination $relative
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    $hash=(Get-FileHash -LiteralPath $file.FullName).Hash
    # 仅共享不应被改写的引擎程序、库与烹饪数据；配置等小文件独立复制。
    if($file.Extension -in '.dll','.pak','.ucas','.utoc','.bin','.dat','.exe'){
        New-Item -ItemType HardLink -Path $target -Target $file.FullName | Out-Null
        $kind='HardLink';$linked++
    }else{Copy-Item -LiteralPath $file.FullName -Destination $target;$kind='Copy';$copied++}
    if((Get-FileHash -LiteralPath $target).Hash -ne $hash){throw '候选文件内容核对失败'}
    $manifest+=[pscustomobject]@{Relative=$relative;Kind=$kind;SHA256=$hash}
}
$targetProgram=Join-Path $Destination $relativeProgram
New-Item -ItemType Directory -Path (Split-Path $targetProgram -Parent) -Force | Out-Null
Copy-Item -LiteralPath $newProgram -Destination $targetProgram
if((Get-FileHash -LiteralPath $targetProgram).Hash -ne $newHash -or (Get-FileHash -LiteralPath $sourceProgram).Hash -ne $SourceProgramSHA256){throw '新程序复制不完整或来源程序发生改变'}
$manifest+=[pscustomobject]@{Relative=$relativeProgram;Kind='NewProgram';SHA256=$newHash}
$manifest | Export-Csv -LiteralPath (Join-Path $Destination 'StageEvidence.csv') -NoTypeInformation
Write-Host ('本地原生代码候选已生成：'+$Destination)
Write-Host ('不可变硬链接='+$linked+' 独立文件='+($copied+1)+' 源资产核对='+$assets.Count+' 程序SHA256='+$newHash)
Write-Host '不要直接改写候选中的共享文件；重新烹饪、资产或反射结构变化必须使用新的完整构建流程。'
