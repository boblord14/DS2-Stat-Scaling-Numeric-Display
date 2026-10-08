# DS2 Stat Scaling Display Fixer

### Overview:
Replaces the stat scaling letters, with the real numeric values used in weapon damage. 

Compensates for the hidden infusion scaling not seen in the menu values as well. 

Works for both armor and weapons.

This code is *moderately untested*. I've compared against a variety of scaling values present on the 
[Dark Souls 2 WikiGG](https://darksouls2.wiki.gg/) and they appear to line up. Please report any errors as I did not 
extensively analyze every permutation for correctness. 

Occasionally values may be off-by-one compared to the wiki. I'm not sure if this is because of the rounding/truncation on 
the scaling calculator sheet used to populate the wiki, or my code. It could be either, and as such I'm not considering it 
an error unless someone can give me a definitive answer to why that occurs. 

### Installation:

This should go without saying, but this only runs on **the latest version of SotFS**. 

Download the latest release zip file from the [releases section](https://github.com/boblord14/DS2-Stat-Scaling-Numeric-Display/releases/latest)

Unzip the folder, and place `dinput8.dll` into the game folder located at
`steamapps\common\Dark Souls II Scholar of the First Sin\Game`. Rename or overwrite the original `dinput8.dll` if need be.  

If you use other `dinput8.dll` mods, consider setting up and using [Lazy Loader](https://www.nexusmods.com/darksouls3/mods/677)
(yes, it works despite being on the DS3 nexus page).

### Uninstallation

Delete `dinput8.dll`, and put the original back in.

If you didn't save it or can't find it, verifying file integrity through steam should do the trick. 

### Credits
Big thanks to `Metalhead` for giving me the original idea to throw something like this together

Huge thanks to `EvanDeadlySins` for answering my inane questions about how the scaling actually works, this wouldn't have
been possible without your knowledge. 