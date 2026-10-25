# Credits and licensing

**MH Tri - Rowell** is maintained by **Rowell
([Dling11](https://github.com/Dling11))**. Rowell-specific changes include the
English app UI, touch input and configurable combos, save browser and backup
retention, Windows keybindings, solo speed controls, lifecycle fixes and build
packaging. This modified source snapshot is dated **25 October 2026**.

The original port and its underlying technology were created by the projects
below. Starting a fresh Git history does not change their authorship or licenses.

| Contributor / project | Contribution and notice |
| --- | --- |
| [krisztsa/OceanTriRecomp](https://github.com/krisztsa/OceanTriRecomp) | Original OceanTriRecomp architecture credited by the Android upstream. |
| [masterd2003/OceanTriRecomp](https://github.com/masterd2003/OceanTriRecomp) | Windows source baseline, commit `46c9bd8a53759b009108c27f63db48f6fe80a9da`. |
| [Kiridor123/OceanTriRecomp-Android](https://github.com/Kiridor123/OceanTriRecomp-Android) | Android port and source baseline, commit `f7b47b7208bfe1febe8d50716359dc682331c9d9`. |
| WiiCompiled contributors | Translator and ABI-derived code, commit `85f250155f8f397c1c463dc51dd4dc2707371d06`; see [origin](toolchain/ORIGIN.md) and [GPL license](toolchain/LICENSE). |
| [Sepalani / MH3SP](https://github.com/sepalani/MH3SP) | Native server adaptation; `runtime/src/mh3sp_native_server.cpp` retains its AGPL-3.0-or-later copyright and license notice. Full text: [AGPL-3.0-or-later](LICENSES/AGPL-3.0-or-later.txt). |
| [Aurora contributors](https://github.com/encounter/aurora) | Application/rendering services; retained [MIT license](aurora-main/LICENSE). |
| [ANGLE Project](https://github.com/google/angle) | Windows graphics dependency, pinned at `b82a564646a26535ccedfa7a69134c0fd6654ac2`; retained [license](third_party/angle/LICENSE). |
| SDL, OpenSSL, FreeType, libpng, zlib, Zstandard, toml11, bzip2 and other dependency authors | Each component retains its own license. See the [third-party notices](toolchain/THIRD-PARTY-NOTICES.md) and notices supplied with dependencies. |

The main source retains its [GNU GPL v3 license](LICENSE). The server component
remains AGPL-3.0-or-later; the root GPL does not replace that component's terms.
Keep copyright/license notices and provide the matching modified source when
distributing builds, following the applicable licenses. The notices above
credit Rowell's modifications without claiming sole authorship of upstream work.

Preserved upstream descriptions: [Windows](docs/upstream/Windows-README.md) and
[Android](docs/upstream/Android-README.md). Their relative links refer to the
original source layouts.

Monster Hunter Tri, its game data and trademarks belong to their respective
rights holders, including Capcom. This project supplies software modifications,
not the game image or game assets, and is not affiliated with Capcom or Nintendo.
