Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios
Import wizard.cocoapods
Public

Class IosAdmob Implements Command
    Method Run:Void(app:App)
        CocoaPods.Init(app)        
        CocoaPods.AddDependency("GoogleMobileAds", app)
        CocoaPods.Install(app)      
        app.LogInfo("Admob added")
    End

End
