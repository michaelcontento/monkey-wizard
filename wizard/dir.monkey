Strict

Private

Import os
Import wizard.helperos

Public

Class Dir
    Private

    Field path:String
    Field parent:Dir

    Public

    Method New(path:String)
        Self.path = RealPath(path)
    End

    Method CopyTo:Void(dstDir:Dir, recursive:Bool=True, hidden:Bool=True, remove:Bool=True)
        If remove And dstDir.Exists() Then dstDir.Remove(True)
        CopyDir(path, dstDir.GetPath(), recursive, hidden)
    End

    Method Parent:Dir()
        If Not parent
            parent = New Dir(ExtractDir(path))
        End

        Return parent
    End

    Method Create:Void()
        If Not Exists() Then CreateDir(path)
    End

    Method Remove:Void(recursive:Bool=True)
        DeleteDir(path, recursive)
    End

    Method Exists:Bool()
        Return DirExists(path)
    End

    Method GetPath:String()
        Return path + "/"
    End
End
