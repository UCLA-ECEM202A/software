## Android Programming Examples.

Examples of Android application development.

Steps to run the examples:

1. Download the [https://github.com/UCLA-ECEM202A/ECEM202A](https://github.com/UCLA-ECEM202A/ECEM202A) repository. Examples are in the _software/android_examples_ folder.

2. Download and install [Android Studio](https://developer.android.com/studio). As of writing these instructions (October 25, 2020), Android Studio is at version 4.1 and these examples worked fine under macOS Catalina and a Google Pixel 4 XL with Android 11.x.

3. If you have an an Android device (with Android version 9.x or higher) and would like to run these apps on the actual phone hardware, then put the device in developer mode and enable USB debugging. Follow the instructions at [https://developer.android.com/studio/debug/dev-options](https://developer.android.com/studio/debug/dev-options). Connect the phone to your computer via an USB cable. If you don't have a phone, that is fine as Android Studio comes with an exmulator, although you won't be able to test the sensors.

4. Using _Configure &rarr; SDK Manager_ download _Android 11.0 (R)_, and using _Configure &rarr; AVD Manager_ download a virtual phone device such as _Pixel 4 XL_.

5. Now open the _SensorDemo_ or the _CameraAppDemo_ project, build it, and run. Before running, select eitherr the  virtual phone device or the real phone.
