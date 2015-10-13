Strict

Private

Import wizard.app
Import wizard.command
Import wizard.dir
Import wizard.file
Import wizard.ios
Import wizard.cocoapods
Public

Class IosFlurry Implements Command
    Method Run:Void(app:App)
        CocoaPods.Init(app)        
        CocoaPods.AddDependency("Flurry-iOS-SDK", app)
        CocoaPods.Install(app)   
        app.LogInfo("Monkey interface can be found here: http://goo.gl/d7jh5")
    End
End
