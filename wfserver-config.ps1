function Show-MainMenu
{
     param (
           [string]$Title = 'wfserver configuration menu'
     )
     #cls
     Write-Host "================ $Title ================"
    
     Write-Host "1: Press '1' to add radio."
     Write-Host "2: Press '2' to delete a radio."
     Write-Host "3: Press '3' to list current radios."
     Write-Host "4: Press '4' to list current users."
     Write-Host "Q: Press 'Q' to quit."
}




$HomePath = 'HKCU:\Software\wfview\wfserver'
$CurrentDir = Get-Location

do
{
     Show-MainMenu
     $input = Read-Host "Please make a selection"
     switch ($input)
     {
           '1' {
                cls
                'You chose option #1'
           } '2' {
                cls
                'You chose option #2'
           } '3' {
                cls
                'Radios available:'
		set-location -Path 'HKCU:\Software\wfview\wfserver\Radios'
		get-childitem
           } '4' {
                cls
                'Users available:'
		set-location -Path 'HKCU:\Software\wfview\wfserver\Server\Users'
		get-childitem
           } 'q' {
		set-location $CurrentDir
                return
           }
     }
     pause
}
until ($input -eq 'q')