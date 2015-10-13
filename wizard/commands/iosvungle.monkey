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

Class IosVungle Implements Command
    Method Run:Void(app:App)
        Ios.NSAllowsArbitraryLoads()

        CocoaPods.Init(app)        
        CocoaPods.AddDependency("VungleSDK-iOS", app)
        CocoaPods.Install(app)        

        app.LogInfo("New Vungle installed!")
    End
   
End
