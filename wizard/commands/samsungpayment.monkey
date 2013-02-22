Strict

Private

Import wizard.android
Import wizard.app
Import wizard.command
Import wizard.file

Public

Class SamsungPayment Implements Command
    Const VERSION:String = "1.95.0"

    Method Run:Void(app:App)
        Android.AddPermission("android.permission.INTERNET")
        Android.AddPermission("android.permission.READ_PHONE_STATE")
        Android.AddPermission("android.permission.GET_ACCOUNTS")
        Android.AddPermission("android.permission.SEND_SMS")
        CopyLibs(app)

        app.LogInfo("Monkey interface can be found here: http://goo.gl/JGoi0")
    End

    Private

    Method CopyLibs:Void(app:App)
        Android.EnsureLibsFolder()

        Local src:File = app.SourceFile("plasma-" + VERSION + ".jar")
        Local dst:File = app.TargetFile("libs/plasma-" + VERSION + ".jar")

        src.CopyTo(dst)
    End
End
