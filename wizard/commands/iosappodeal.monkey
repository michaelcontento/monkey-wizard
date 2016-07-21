Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios
Import os
Import wizard.cocoapods

Public

Class IosAppodeal Implements Command
    Method Run:Void(app:App)
        Ios.NSAllowsArbitraryLoads()
        CocoaPods.Init(app)        
        CocoaPods.AddSource("https://github.com/appodeal/CocoaPods.git", app)
        CocoaPods.AddSource("https://github.com/CocoaPods/Specs.git", app)
        CocoaPods.AddDependency("Appodeal", app, "0.8.1")
        CocoaPods.AddDependency("MWFeedParser", app, "1.0")
        CocoaPods.Install(app)        

        app.LogInfo("New AppODeal installed!")
    End
   
End
