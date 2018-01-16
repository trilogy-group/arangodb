#REQUIRES -Version 2.0
<#
.SYNOPSIS
    Common base of ArangoDB Windows build scripts.
.DESCRIPTION
    The Script Sets variables and executes CMake with a fitting
    generator and parameters required by the build. It optionally
    starts a build if the $do_build variable is set to $TRUE.
#>

# set variables
$ErrorActionPreference = "Stop"
$PSDefaultParameterValues['*:ErrorAction']='Stop'

Import-Module VSSetup

$arango_source = split-path -parent $PSScriptRoot

$cl = $(Get-ChildItem $(Get-VSSetupInstance).InstallationPath -Filter cl.exe -Recurse | Select-Object Fullname |Where {$_.FullName -match "Hostx64\\x64"}).FullName
$cl_path = Split-Path -Parent $cl

$env:GYP_MSVS_OVERRIDE_PATH=$cl_path
$env:CC=$cl
$env:CXX=$cl

# print configuration
Write-Host "build                 :"$build
Write-Host "skip_packaging        :"$skip_packaging
Write-Host "extra args            :"$Args
Write-Host "configuration         :"$config
Write-Host "source path           :"$arango_source
Write-Host "generator             :"$generator
Write-Host "VC version            :"$vc_version
Write-Host "CC                    :"$env:CC
Write-Host "CXX                   :"$env:CXX
Write-Host "GYP_MSVS_OVERRIDE_PATH:"$cl_path

Start-Sleep -s 2

# configure
cmake -G "$generator" -DCMAKE_BUILD_TYPE="$config" -DSKIP_PACKAGING="$skip_packaging" $Args "$arango_source"

# build - with msbuild
if ($build) {
    cmake --build . --config "$config"
}