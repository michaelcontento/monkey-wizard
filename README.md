# [Monkey-Wizard][]

Like it? Support it! 

[![Flattr this git repo](http://api.flattr.com/button/flattr-badge-large.png)](https://flattr.com/submit/auto?user_id=kaffeefleck&url=https://github.com/michaelcontento/monkey-wizard&title=Monkey-Wizard&language=en_GB&tags=github&category=software)

## What?

This tool can be used to configure / inject third party libs into [Monkey][]
based projects.

<iframe style="border: 0; margin: 0; padding: 0;"
        src="https://www.gittip.com/michaelcontento/widget.html"
        width="48pt" height="22pt"></iframe>

## Why?

Because it's quite tedious to do this manually over and over again. This way is
much faster and won't accidentally skip some important parts.

## Details?

### Requirements
* Monkey
* CocoaPods (for ios)

### How to compile?

    make build

### How to run?

Just execute `make run` and you should see something like this:

    ./wizard.build/stdcpp/main_macos
    Usage: wizard COMMAND TARGETDIR [COMMAND SPECIFIC OPTIONS]

    Commands:
      AmazonPayment
      AndroidAntKey
      AndroidIcons
      AndroidRevmob
      AndroidVersion
      GooglePayment
      IosAppirater
      IosBundleId
      IosCompressPngFiles
      IosDeploymentTarget
      IosFlurry
      IosFlurryAds
      IosFramework
      IosHideStatusBar
      IosIcons
      IosInterfaceOrientation
      IosLaunchImage
      IosProductName
      IosRevmob
      IosVersion
      SamsungPayment

    ERRO: Invalid number of arguments
    make: *** [run] Error 2

As you can see you need to specify the desired command and the target directoy,
in which the wizard should perfom it's magic. So to do some real work just
execute

	make run ARGS="googlepayment ../some-project/project.build/android"
	
and the result should be:

    ./wizard.build/stdcpp/main_macos

Now go and look at the results! :)

### Can I add new commands?

Sure! Just follow this steps:

* Create a new monkey file `wizard/commands/$COMMAND.monkey`
* Implement the `Command` interface (see: `wizard/command.monkey`)
* Execute `make commandsimport` to get the new command recognized
* (Optional) Put static files into `wizard.data/commands/$COMMAND/`

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
