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
        CocoaPods.AddDependency("Appodeal", app, "0.10.2")
        CocoaPods.AddDependency("MWFeedParser", app, "1.0")
        CocoaPods.Install(app)        

        Ios.AddToPlist("NSBluetoothPeripheralUsageDescription", "Advertising")
        Ios.AddToPlist("NSCalendarsUsageDescription", "Advertising")
        Ios.AddToPlist("NSLocationWhenInUseUsageDescription", "Advertising")
        Ios.AddToPlist("NSPhotoLibraryUsageDescription", "Advertising")


        app.LogInfo("New AppODeal installed!")
    End
   
End
