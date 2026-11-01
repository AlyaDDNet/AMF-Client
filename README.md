<div align="center">

# AMF Client — Nova Edition

### Comfort. Control. Quality.

**A customizable DDNet client focused on gameplay, personalization, performance, and meaningful run statistics.**

[![Latest Release](https://img.shields.io/github/v/release/AlyaDDNet/AMF-Client?label=Latest%20Release)](https://github.com/AlyaDDNet/AMF-Client/releases/latest)
[![GitHub Downloads](https://img.shields.io/github/downloads/AlyaDDNet/AMF-Client/total?label=Downloads)](https://github.com/AlyaDDNet/AMF-Client/releases)
[![GitHub Stars](https://img.shields.io/github/stars/AlyaDDNet/AMF-Client?style=flat&label=Stars)](https://github.com/AlyaDDNet/AMF-Client/stargazers)

**[Download AMF Client](https://github.com/AlyaDDNet/AMF-Client/releases/latest)** · **[Official Website](https://alyaddnet.github.io/amfclient/)** · **[Source Code](https://github.com/AlyaDDNet/AMF-Client)**

</div>

---

## What is AMF Client?

**AMF Client** is a custom client for **DDraceNetwork (DDNet)**, developed by **[alya?](https://github.com/AlyaDDNet)** and based on **BestClient**. It builds on the DDNet ecosystem with a distinctive **Nova Edition** interface, extensive customization options, and original systems designed to make gameplay feedback more useful.

Whether you're practicing difficult maps, playing with a team, or adjusting the client to your preferences, AMF Client aims to make the experience more comfortable and informative.

> **Version note:** The public release and the current development source may differ. Features under active development are not necessarily included in the [latest published download](https://github.com/AlyaDDNet/AMF-Client/releases/latest).

## Key Features

### AMF Impact System & Run Statistics

The **AMF Impact System** is an original AMF Client feature for analyzing contributions during team runs. It includes:

- **Saves** — successful teammate rescues.
- **Aleds** — a specialized rescue contribution tracked by the system.
- **Clutches** — crucial saves when you're the last surviving teammate.
- **Falls** — tracked run failures and fall events.
- **Consistency** — a measure of performance throughout the run.
- **Rating & MVP** — contribution-based evaluation and a standout player for the run.

The run statistics interface brings these indicators together so players can better understand what happened during a run.

### HUD Customization

Customize the placement and presentation of supported HUD elements using the **HUD Editor**, including elements such as the timer, killfeed, and finish prediction display.

### Nova Edition Interface

A redesigned client experience with a soft blue, white, and purple visual identity, clearer settings organization, and a focus on usability.

### Personalization & Quality-of-Life Features

AMF Client extends the customization options available in its DDNet/BestClient foundations, with additional client settings, visual adjustments, and gameplay conveniences.

### Performance & Controls

Includes performance-oriented settings and client refinements intended to improve responsiveness and everyday play. Actual performance depends on hardware, configuration, and server conditions.

## Download & Installation

**Get the latest public version:** [AMF Client Releases](https://github.com/AlyaDDNet/AMF-Client/releases/latest)

1. Open the latest release page.
2. Download the Windows Portable archive provided for that release.
3. Extract the archive to a folder of your choice.
4. Launch the client executable included in the extracted package.

> **Important:** Download AMF Client from the official repository or its linked website. Local development builds can contain newer changes than the public release.

## Screenshots

Screenshots of the Nova Edition interface, HUD Editor, and Run Statistics will be added here. Only verified screenshots of the actual client should be used.

## Building from Source

The source tree is derived from DDNet and retains its CMake-based build system. General requirements and platform-specific instructions are available in the upstream documentation:

- [Building Guide](docs/BUILDING.md)
- [Debugging Guide](docs/DEBUGGING.md)
- [Contributing Guide](docs/CONTRIBUTING.md)

### Clone AMF Client

```bash
git clone --recursive https://github.com/AlyaDDNet/AMF-Client.git
cd AMF-Client
```

### Windows x64 Release

Install Visual Studio with C++ build tools, CMake, Python, Rust, and the relevant SDKs. For a configuration with Steam, Discord, and Vulkan integration:

```bash
cmake -S . -B build -A x64 -DDEV=OFF -DSTEAM=ON -DDISCORD=ON -DVULKAN=ON
cmake --build build --config Release --target game-client game-server amfclient-updater amfclient-priority-restore
```

Vulkan builds require the Vulkan SDK, including `glslangValidator`. If it is unavailable, configure with `-DVULKAN=OFF` instead. Available targets and dependencies can vary with the source revision and local toolchain.

For Linux and macOS build prerequisites, refer to [docs/BUILDING.md](docs/BUILDING.md) and the relevant CMake configuration. **A successful source build is not a promise that an official binary is currently published for that platform.**

## Credits & Acknowledgments

- **AMF Client:** developed by [alya?](https://github.com/AlyaDDNet).
- **BestClient:** the project AMF Client is based on; credits and licensing remain with its respective authors and contributors.
- **DDNet / DDraceNetwork:** the underlying open-source game and development community.
- **T-Client and other upstream contributors:** acknowledgment for functionality and code inherited through the project's upstream lineage, where applicable.

AMF Client is an independent community project and is not the official DDNet client.

## Licensing & Attribution

This project contains code derived from upstream projects. **Different components may be subject to different licenses.** Review the repository's license files, upstream notices, and component-specific terms before copying, modifying, or redistributing code.

### AMF Impact / Run Statistics

**AMF Impact is an original feature developed for AMF Client by alya?.**

If you plan to study, modify, reuse, or integrate this system into another client or project, read **[AMF Impact License](licenses/AMF-Impact-LICENSE.txt)** first and preserve the required attribution.

Copyright © 2026 alya? — applicable rights and terms are described in the relevant license files.

## Related Links

- [AMF Client GitHub Repository](https://github.com/AlyaDDNet/AMF-Client)
- [AMF Client Releases](https://github.com/AlyaDDNet/AMF-Client/releases)
- [AMF Client Website](https://alyaddnet.github.io/amfclient/)
- [DDNet Official Website](https://ddnet.org/)
- [DDNet Source Code](https://github.com/ddnet/ddnet)
- [BestClient Website](https://bestclient.fun/)

---

<div align="center">

**AMF Client — Nova Edition**

*Comfort. Control. Quality.*

</div>
