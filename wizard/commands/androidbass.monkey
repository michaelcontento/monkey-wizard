Strict

Private

Import wizard.android
Import wizard.app
Import wizard.command

Public

Class AndroidBass Implements Command

    Method Run:Void(app:App)
        CopyLibs(app)
    End

    Private

    Method CopyLibs:Void(app:App)
        Android.EnsureLibsFolder()

        app.TargetDir("libs/armeabi").Create()
        app.SourceFile("libs/armeabi/libbass.so").CopyTo(app.TargetFile("libs/armeabi/libbass.so"))

        app.TargetDir("libs/armeabi-v7a").Create()
        app.SourceFile("libs/armeabi-v7a/libbass.so").CopyTo(app.TargetFile("libs/armeabi-v7a/libbass.so"))

        app.TargetDir("src/com/un4seen").Create()
        app.TargetDir("src/com/un4seen/bass").Create()
        app.SourceFile("src/com/un4seen/bass/BASS.java").CopyTo(app.TargetFile("src/com/un4seen/bass/BASS.java"))
    End
End
