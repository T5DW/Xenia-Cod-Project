# CAMOTOOL (camo-dumper) — by T5DW
Part of the Xenia-COD-Project.

camotool finds your Black Ops II July 20 2012 MP prototype folder, exports its camo textures to PNG, and swaps them for your own images by rewriting common_mp.ff. It runs locally, offline, against your own copy of the game.

What does this do?
Finds the game folder. It looks for the folder with common_mp.ff: next to the exe, then Downloads, Desktop and Documents, then every drive. If it can't find it, paste or drag the folder in. Your choice is saved in camotool.cfg.
Decompiles the camos. It unpacks common_mp.ff, finds every camo texture and exports it as a PNG.
Replaces a camo. Pick a camo, drag in a PNG or JPG of any name and any size, and it is resized, converted to the game's texture format and written back into common_mp.ff.
Restores the original. The first replace saves the untouched file as common_mp.ff.original, and one option puts it back.
What games and game versions do we support?
Build	Game
July 20 2012 MP prototype	Call of Duty: Black Ops II (BO2), Xbox 360
It is run through Xenia (title 415608C3, module hash ABAB4A29FBBA808C).

How do I use it?
Put camotool.exe anywhere and double-click it.
Let it find your game folder, or paste or drag the folder in.
Pick an option from the menu:
1 lists every camo texture and whether it can be replaced.
2 exports the replaceable ones to camos_export\ next to the exe.
3 replaces one. Pick its number, then drag in an image and press Enter.
4 puts the original common_mp.ff back.
5 writes camotool_ipak_report.txt about common_mp.ipak.
6 changes the game folder.
Restart the game in Xenia.
The command line works too:

camotool [--game <folder>] list | export [--mips] | replace <number|name> <image> | restore | ipak
Do I need a patch?
Yes. The game checks an RSA signature on every .ff file, and any edited file fails that check, so the game crashes at the load screen. Turn on the Allow modified fastfiles patch in 415608C3 - Black Ops II July 20 Prototype (unlock all).patch.toml and put that file in Xenia's patches folder. The same file also has Unlock all items and Purchase all items, which give a level 1 profile Create-a-Class and every item.

Check that the Xenia log shows Patcher: Applying patch for: ... - Allow modified fastfiles. If it says PatchDB: Loaded patches for 0 titles, Xenia didn't find the patch file.

What can be replaced?
Texture	In common_mp.ff	Replaceable now
Gold (camo_gold_c, 32x32)	yes	yes
Far-away tints (solid_camo_*)	yes	yes, set to your image's average colour
No-camo layers (weapon_camo_off, weapon_camo_neutral, 128x128)	yes	yes, experimental
Character camos (camo_jungle_us etc., 256x256)	yes	yes, but these aren't on guns
Nevada, Sahara, ERDL, Tiger, Berlin, Warsaw, Siberia, Yukon, Woodland, Flora (256x256)	header only	no, the pixels are streamed from common_mp.ipak
The in-game names (Berlin = urban_german and so on) come from the camo strings in this build. They haven't been confirmed in game.

How does it work?
src/fastfile.cpp: the .ff is a TAffx100 file of Salsa20-encrypted blocks. The IV table is built from the zone name, and each block's SHA-1 is chained into it across 4 streams. Each block is an XMem LZX stream that unpacks to 0xFFC0 bytes. Changed blocks are stored again as uncompressed LZX blocks, everything after them is re-encrypted, and the new file is read back and checked before it replaces the original.
src/images.cpp: finds the camo GfxImage headers in the zone, reads the Xenos fetch constant (pitch, size, mip address), and untiles and retiles the DXT data with the addressing code from Xenia.
src/ipak.cpp: writes the report on common_mp.ipak.
src/main.cpp: folder detection, the menu and the command line.
How do I build it?
You need xmake and a C++17 compiler. Run these from camo-dumper/camotool (put xmake.lua there first):

xmake            # build (MSVC on Windows)
xmake run camotool
To cross-build with MinGW from Linux, run xmake f -p mingw -a x86_64 --mingw=/usr && xmake.

Third party code:

libmspack lzxd.c (LGPL 2.1, third_party/libmspack)
stb_image, stb_image_write and stb_dxt (public domain)
tiling math ported from Xenia (BSD)
What's not done yet?
Camos on guns. This build has no camo slot in Create-a-Class, and the game's weapon-options code appears to ignore camo. So a replaced camo can't be equipped on a gun the normal way yet. Changing a texture changes it everywhere that texture is used.
Streamed camos. The 10 main camos live in common_mp.ipak. Option 5 writes a report about that file, and the report is what ipak support will be built from.
Graphics crash. Some setups get Xenia's "Graphics device lost" when a match starts. It is still being tracked down. If it happens, restore the original common_mp.ff with option 4.
— T5DW
