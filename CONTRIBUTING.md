# How to contribute to PITHA

Thank you for helping improve **PITHA** (*PineTime InfiniTime Home Assistant*), a fork of [InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime)!

> [!NOTE]
> If your bug report or feature request applies to upstream InfiniTime (and not PITHA-specific features such as the Home Assistant integration, watch faces, or extra apps), please check whether it belongs on [InfiniTimeOrg/InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime) as well.

All contributions to PITHA are accepted under the **GNU General Public License v3.0 or later** ([LICENSE](LICENSE)), the same license as InfiniTime.

Here are things you can do to help:

- [Report bugs or issues](https://github.com/renato-grottesi/pitha/issues/new/choose)
- [Write and improve documentation](#documentation)
- [Fix bugs, add functionalities and improve the code](#how-to-create-a-pull-request)

## Documentation

Documentation improvements, setup guides, and photos/diagrams are welcome. Submit changes via a [pull request](#how-to-create-a-pull-request).

## How to create a pull request

The workflow is based on [GitHub flow](https://docs.github.com/en/get-started/quickstart/github-flow):

1. Fork [renato-grottesi/pitha](https://github.com/renato-grottesi/pitha).
2. Create a new branch from `main`.
3. Make your changes and test them on a PineTime (or simulator) and/or Home Assistant.
4. Commit your changes with a clear description.
5. Open a pull request against `renato-grottesi/pitha`.

### PR checklist

- Keep flash/RAM impact minimal (PineTime has 464 KB usable app flash and 64 KB SRAM).
- Conform to the [coding conventions](doc/coding-convention.md).
- For firmware changes, compile cleanly with zero warnings (`-Wall -Wextra -Werror` is enabled).
- For Home Assistant integration changes, ensure HACS & hassfest checks pass.
- Do not include personal Bluetooth MAC addresses, IP addresses, or credentials in code, docs, or configs.
