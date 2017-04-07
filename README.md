# [Monkey-Wizard][]

## What?

This tool can be used to configure / inject third party libs into [Monkey][]
based projects.

## Why?

Because it's quite tedious to do this manually over and over again. This way is
much faster and won't accidentally skip some important parts.

## Details?

### Requirements

* Monkey
* CocoaPods (for ios)
* Steam SDK (optional, put it in wizard.data/commands/glfwsteam directory)

### How to compile?

    $ make build

### How to run?

    $ make build
    $ ./wizard.build/cpptool/main_macos

You should now see something like this:

    Usage: wizard COMMAND TARGETDIR [COMMAND SPECIFIC OPTIONS]

    Commands:
      AmazonAds
      AmazonPayment
      AndroidAntKey
      AndroidBass
      AndroidIcons
      AndroidSetTargetSdk
      AndroidVersion
      IosAddLanguage
      IosAppodeal
      IosBundleId
      IosCocoapods
      IosCompressPngFiles
      IosDeploymentTarget
      IosEnableBitcode
      IosFramework
      IosHideStatusBar
      IosIcons
      IosInterfaceOrientation
      IosLaunchImage
      IosPatchCodeSigningIdentity
      IosProductName
      IosRequiresFullscreen
      IosVersion

    ERROR: Invalid number of arguments

As you can see you need to specify the desired command and the target directoy,
in which the wizard should perfom it's magic. So to do some real work just
execute:

	$ ./wizard.build/cpptool/main_macos googlepayment ../some-project/project.build/android

Now go and look at the results! :)

### Can I add new commands?

Sure! Just follow this steps:

* Create a new monkey file `wizard/commands/$COMMAND.monkey`
* Implement the `Command` interface (see: `wizard/command.monkey`)
* **Optional:** Put static files into `wizard.data/commands/$COMMAND/`
* `make build` as described above

## License?

    Copyright 2012 Michael Contento <michaelcontento@gmail.com>

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

  [Monkey]: http://www.monkeycoder.co.nz/
  [Monkey-Wizard]: https://github.com/michaelcontento/monkey-wizard
