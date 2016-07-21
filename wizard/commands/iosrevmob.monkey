Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios
Import wizard.cocoapods
Public

Class IosRevmob Implements Command
    Method Run:Void(app:App)
        Ios.NSAllowsArbitraryLoads()

        CocoaPods.Init(app)        
        CocoaPods.AddDependency("RevMob", app)
        CocoaPods.Install(app)        
        app.LogInfo("RevMob: Monkey interface can be found here: http://goo.gl/yVTiV")
    End
End
