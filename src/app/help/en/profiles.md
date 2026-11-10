A profile is a set of keys for a game. Your profiles are on the **Profiles** page.

## Make a profile

On the **Profiles** page, select **New profile…** and pick a starting point:

- :layout-grid: **Use a template**: choose the closest kind of game—Shooter, MOBA, Racing, Action / RPG, Cards / Tapping or Blank. Give the profile a name, link it to a game and choose **Landscape** or **Portrait**. If a phone is mirroring, **Create and edit** opens the editor on it right away.
- :puzzle: **Community profiles**: start from a profile that an installed extension provides, or import a file someone shared.
- :plus: **Start blank**: place every key yourself.

When a game without a profile is in front on a mirrored phone, the mirror window offers the same choices. If you close that offer, it does not come back by itself for this game; **Key mapping options > New profile…** on the toolbar opens it again.

Then put the keys on the game's buttons: [The key mapping editor](help:keymap-editor)

## Use a profile

- **In the mirror window**: open **Key mapping options** on the toolbar and pick a profile. Profiles linked to the game in front are marked **This game**; **No profile** uses the phone without one.
- **On the Profiles page**: select **Use on** on a profile's card. With several phones mirroring, choose the phone.

## Link a profile to a game

When **Switch profiles with the game** is on (in **Settings > Input & key mapping**, on by default), the phone uses the profile linked to the game in front by itself. If you pick another profile for a game, the phone remembers your choice for that game.

![Once a profile is linked to a game, it is used whenever that game comes to the front.](figure:profile-link)

To link a profile, open **More** (…) on its card and choose **Link to …**. The menu lists the games running on your mirrored phones. **Unlink from …** removes the link.

## Find and tidy

- Search by profile name or by game.
- Filter with **All**, **This game**, **Mine**, **From extensions** and **Problems**.
- **Duplicate** makes a copy.
- **Delete…** moves the profile to the Recycle Bin. Phones using it stop using it.
- **Show in folder** shows the profile's file in File Explorer; **Open folder** opens the folder that holds all your profiles.

A profile with a problem shows what is wrong on its card.

## Profiles from extensions

Extensions can bring profiles. They are marked **Read-only**, but you can still edit them: your changes are kept separately, and **Restore original…** removes them. **Copy to mine** makes a copy that is all yours.

## Share a profile

A profile is either a single `.json` file or a profile folder. A profile folder holds `profile.json` with the keys, and can also hold logic (`logic.ts` or `logic.js`) and pictures (see [Getting started with scripts](help:scripts)).

![A profile folder is exported as one .gsprofile file; send it to a friend, who imports it on the Profiles page.](figure:profile-share)

- **To share**: **Export…** in **More** on the card. A profile folder is saved as a `.gsprofile` file, which keeps its logic and pictures; you can also save just its keys as `.json`.
- **To receive**: **Import…** on the **Profiles** page opens `.json` and `.gsprofile` files. You can also drop files, or a profile folder, onto the GameScrcpy window.

> [!WARNING]
> **Only import profiles you trust**: a profile with logic can tap, swipe and press keys on the phone by itself, and can read the keys you hold and where the pointer is in the mirror window. GameScrcpy asks before it imports one.
