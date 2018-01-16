#REQUIRES -Version 2.0
<#
.SYNOPSIS
    Configures and builds ArangoDB
.EXAMPLE
    mkdir arango-build; cd arangod-build; ../arangodb/configure/<this_file> [-build] [-packaging] [cmake params]
#>
param([switch] $build, [switch] $packaging)

if($packaging)
{$skip_packaging = "OFF"}
else
{$skip_packaging = "ON"} 

$config = "Debug"

$vc_version = "15.0"
$generator = "Visual Studio 15 2017 Win64"

. "$PSScriptRoot/windows_common.ps1" $Args