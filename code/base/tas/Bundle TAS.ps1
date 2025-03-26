if(Get-Item tas_scripts.pk3)
{
    Remove-Item tas_scripts.pk3
}

$compress = @{
  Path = "*.tas"
  CompressionLevel = "Fastest"
  DestinationPath = "tas_scripts.zip"
}
Compress-Archive @compress

Move-Item tas_scripts.zip tas_scripts.pk3