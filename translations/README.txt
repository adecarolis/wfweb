Steps to contribute a translation: 

1. Sign up for a free GitLab.com account

2. Fork the wfview repository. Be sure to work from our current development branch. 

3. Clone the repository to your desktop computer. 

4. Install Qt Linguist. This is a free program from the Qt company. 
    Windows: https://download.qt.io/linguist_releases/ (an older release but a small stand-alone package)
    linux (qt6): sudo apt install linguist-qt6
    linux (qt5): sudo apt install qttools5-dev-tools
    macOS: Likely available in macports

5. Open Qt Linguist

6. Open the translation ".ts" file for your language of choice. Important: If your language is not available, you should probably ask the wfview team to generate the blank ".ts" file for you. Alternativly, if you are comfortable with developer tools, you can generate it yourself:

6a: Add the filename to the list in the wfview.pro file where you see "TRANSLATIONS". 
6b: using the `lupdate` command, which can be run from Qt Creator using Tools --> External --> Linguist --> Update Translations (lupdate). 

7. Insert translations for as many strings as you can. Press Control-Enter after each one to "accept" the translation and move to the next string. Try and adhere to the following: 

7a: Don't bother translating common technical terms like "PTT" and "VFO". 
7b: If at all possible, keep the translated text at or near the length of the text you are replacing. 
7c: When unsure, add a comment in Qt Linguist. Any text left untranslated simply reverts to English. 

8. When you are finished, commit the change and then push it back to your git repository. 

9. Submit a "Pull Request" ("PR") to our team, notifying us that there is a translation to accept into wfview. 

If the gitlab process is too complicated, simply email a team member your translated files. We understand! 

If you'd like to be credited for the translation, we are happy to do so, please indicate how you would like to be named. 

If you wish to preview your changes within wfview, you will need to setup a complete build environment first, and then run `lrelease` to build the ts files into "qm" files. Next, verify that the qm file is included in the translations resource file (translation.qrc). To quickly change languages in linux, set the LANG and LANGUAGE environment variables in your shell to the language code you wish to test. For example, "export LANG=jp; export LANGUAGE=jp; wfview". You should make a note of these variables' initial values, or just close the terminal when finished. 

Thanks for your contributions to wfview! 

--The wfview team
