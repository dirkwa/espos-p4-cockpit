# Changelog

## [1.10.0](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.9.1...v1.10.0) (2026-11-28)


### Added

* **voice:** run voice only where the layout asks ([#243](https://github.com/dirkwa/espos-p4-cockpit/issues/243)) ([98ee2b4](https://github.com/dirkwa/espos-p4-cockpit/commit/98ee2b4aa84703e0f607394f3effeeb9b4ba453a))


### Fixed

* bump espOS to v0.33.0 ([#245](https://github.com/dirkwa/espos-p4-cockpit/issues/245)) ([16fd24e](https://github.com/dirkwa/espos-p4-cockpit/commit/16fd24e94fa2fc93b535ae6f371b771f99cc7003))

## [1.9.1](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.9.0...v1.9.1) (2026-11-27)


### Fixed

* bump espOS to v0.32.3 ([#241](https://github.com/dirkwa/espos-p4-cockpit/issues/241)) ([7109a61](https://github.com/dirkwa/espos-p4-cockpit/commit/7109a617ea004781c40f1e11c450b462ef3e25ad))

## [1.9.0](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.8.1...v1.9.0) (2026-11-26)


### ⚠ BREAKING CHANGES

* **net:** the layout API moves from port 8081 to the espOS web server's port (80 by default) and cockpit.api_port is gone. Point the designer at http://<panel> without a port. With httpd.api_key set, everything except /hello and /healthz needs the key.

### Added

* **fonts:** draw umlauts and typographic marks ([#237](https://github.com/dirkwa/espos-p4-cockpit/issues/237)) ([03378b6](https://github.com/dirkwa/espos-p4-cockpit/commit/03378b6b210706660b77a89c154fc2ea27d45def))
* **layout:** add a read-only mode to the player ([#235](https://github.com/dirkwa/espos-p4-cockpit/issues/235)) ([ce259cd](https://github.com/dirkwa/espos-p4-cockpit/commit/ce259cd55fbd41c8678bd6c5d6c1f4f662f2dc79))
* **widgets:** show a dash until a value arrives ([#238](https://github.com/dirkwa/espos-p4-cockpit/issues/238)) ([522abb4](https://github.com/dirkwa/espos-p4-cockpit/commit/522abb4954852295fe0c469603d0819b9cca64a7))


### Fixed

* bump espOS to v0.32.1 ([#234](https://github.com/dirkwa/espos-p4-cockpit/issues/234)) ([4201767](https://github.com/dirkwa/espos-p4-cockpit/commit/420176744fbad61c9e2b2eeee10c8d3fae035a22))
* bump espOS to v0.32.2 ([#240](https://github.com/dirkwa/espos-p4-cockpit/issues/240)) ([7ad2548](https://github.com/dirkwa/espos-p4-cockpit/commit/7ad254822efcdf669841af93386bfd605368450d))
* **widgets:** show arc bands inside the track ([#239](https://github.com/dirkwa/espos-p4-cockpit/issues/239)) ([7a9e391](https://github.com/dirkwa/espos-p4-cockpit/commit/7a9e39188b357ac2d263181641f4bcf2978d9944))


### Changed

* **net:** serve the layout API from the espOS httpd ([#232](https://github.com/dirkwa/espos-p4-cockpit/issues/232)) ([af93bb3](https://github.com/dirkwa/espos-p4-cockpit/commit/af93bb345a84ccbb33271a828dc0178df7637594))


### Internal

* release the port move as 1.9.0 ([#236](https://github.com/dirkwa/espos-p4-cockpit/issues/236)) ([8aeed1a](https://github.com/dirkwa/espos-p4-cockpit/commit/8aeed1ac8efa2b56a2ffcb7ef21ad24f5638971f))

## [1.8.1](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.8.0...v1.8.1) (2026-11-25)


### Fixed

* bump espOS to v0.32.0 ([#224](https://github.com/dirkwa/espos-p4-cockpit/issues/224)) ([47db286](https://github.com/dirkwa/espos-p4-cockpit/commit/47db286ae1b0f17855edfa3148757efb6bce1344))
* size the socket table for the panel ([#228](https://github.com/dirkwa/espos-p4-cockpit/issues/228)) ([9fbfa61](https://github.com/dirkwa/espos-p4-cockpit/commit/9fbfa61720301e1380e13ad4ce9e9521fd312d5a))
* **wake:** keep wake discovery off the token ([#227](https://github.com/dirkwa/espos-p4-cockpit/issues/227)) ([6c32dea](https://github.com/dirkwa/espos-p4-cockpit/commit/6c32deada14c38c6fea69fc78f1c9f6bed669ce6))


### Changed

* **layout:** share the layout player with wasm ([#225](https://github.com/dirkwa/espos-p4-cockpit/issues/225)) ([2db663e](https://github.com/dirkwa/espos-p4-cockpit/commit/2db663e7ba383ca8a4fb17a2279fca214e185a17))

## [1.8.0](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.7.0...v1.8.0) (2026-11-19)


### Added

* **layout:** tab strip as overlay, page swipes ([#211](https://github.com/dirkwa/espos-p4-cockpit/issues/211)) ([23ea06d](https://github.com/dirkwa/espos-p4-cockpit/commit/23ea06d93c78d71c25e2e220bd407f6c198007c7))


### Fixed

* bump espOS to v0.29.2 ([#219](https://github.com/dirkwa/espos-p4-cockpit/issues/219)) ([905d20b](https://github.com/dirkwa/espos-p4-cockpit/commit/905d20b7b1fbc30d162e98cece85495b15d5abdf))
* bump espOS to v0.29.3 ([#222](https://github.com/dirkwa/espos-p4-cockpit/issues/222)) ([fa57573](https://github.com/dirkwa/espos-p4-cockpit/commit/fa57573641997017db9e6511d55dc0b0617af559))
* **jlp:** harden incoming signal k values ([#202](https://github.com/dirkwa/espos-p4-cockpit/issues/202)) ([8947f75](https://github.com/dirkwa/espos-p4-cockpit/commit/8947f75d3314d64dbdd385b37b185fbe33699d58))
* **jlp:** ignore callbacks of ended subscriptions ([#221](https://github.com/dirkwa/espos-p4-cockpit/issues/221)) ([274189e](https://github.com/dirkwa/espos-p4-cockpit/commit/274189e28b17d48038fcd02f962dfdde56341fa3))

## [1.7.0](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.6.0...v1.7.0) (2026-11-14)


### Added

* **jlp:** set show, sound and repeat per level ([#208](https://github.com/dirkwa/espos-p4-cockpit/issues/208)) ([0ff98ba](https://github.com/dirkwa/espos-p4-cockpit/commit/0ff98ba7b5093407d86d18d8597a24464c73f246))
* **settings:** brightness, theme and screen off menu ([#216](https://github.com/dirkwa/espos-p4-cockpit/issues/216)) ([c3f9c63](https://github.com/dirkwa/espos-p4-cockpit/commit/c3f9c63072a76f270c36f9e400ad7578c785c520))
* **theme:** day, dark and night panel themes ([#215](https://github.com/dirkwa/espos-p4-cockpit/issues/215)) ([b79c646](https://github.com/dirkwa/espos-p4-cockpit/commit/b79c646edf6979159d75d543a271c3391387015b))


### Fixed

* bump espOS to v0.26.1 ([#210](https://github.com/dirkwa/espos-p4-cockpit/issues/210)) ([ccd058b](https://github.com/dirkwa/espos-p4-cockpit/commit/ccd058b81e24663a6b77e3064775284154be39fe))
* bump espOS to v0.29.0 ([#214](https://github.com/dirkwa/espos-p4-cockpit/issues/214)) ([375606c](https://github.com/dirkwa/espos-p4-cockpit/commit/375606c2c4451d55282de377e160c21553fbe995))
* close two cross-task races ([#204](https://github.com/dirkwa/espos-p4-cockpit/issues/204)) ([cfd5843](https://github.com/dirkwa/espos-p4-cockpit/commit/cfd58434b6f3dce35e19aec1610c45b12d3188cd))
* **jlp:** expire the ack echo guard ([#203](https://github.com/dirkwa/espos-p4-cockpit/issues/203)) ([2c68d0c](https://github.com/dirkwa/espos-p4-cockpit/commit/2c68d0c1812c13026fb561bcfb804c3e8d23abbc))
* **layout:** keep the pages off the tab strip ([#205](https://github.com/dirkwa/espos-p4-cockpit/issues/205)) ([55813d2](https://github.com/dirkwa/espos-p4-cockpit/commit/55813d2b5de5c55f9a3665a5be46b900d2345dc9))
* **net:** bound the http handler waits ([#200](https://github.com/dirkwa/espos-p4-cockpit/issues/200)) ([5d89897](https://github.com/dirkwa/espos-p4-cockpit/commit/5d8989773c2a210d0f5de4fe2391f76f4a72023c))
* **net:** cap api_port so ctrl_port fits ([#201](https://github.com/dirkwa/espos-p4-cockpit/issues/201)) ([0fa5fea](https://github.com/dirkwa/espos-p4-cockpit/commit/0fa5fea850b97b79a9cc798d478108ff8fd6b0b3))

## [1.6.0](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.5.6...v1.6.0) (2026-11-01)


### Added

* **license:** relicense under Apache-2.0 ([#198](https://github.com/dirkwa/espos-p4-cockpit/issues/198)) ([c3eaeed](https://github.com/dirkwa/espos-p4-cockpit/commit/c3eaeed9feaeb747ea90f3871bea04b74650019d))


### Fixed

* bump espOS to v0.24.0 ([#192](https://github.com/dirkwa/espos-p4-cockpit/issues/192)) ([0f28898](https://github.com/dirkwa/espos-p4-cockpit/commit/0f28898910fddb8e2efff78907f2ba76d69f26c9))
* bump espOS to v0.26.0 ([#199](https://github.com/dirkwa/espos-p4-cockpit/issues/199)) ([916483d](https://github.com/dirkwa/espos-p4-cockpit/commit/916483df650730dca7d730da86bd59b4722650e1))
* keep the legacy platformio_local.ini ignored ([#196](https://github.com/dirkwa/espos-p4-cockpit/issues/196)) ([f5197e2](https://github.com/dirkwa/espos-p4-cockpit/commit/f5197e2307458bb8fd2204cf04bf4ebe57b6fd50))


### Changed

* drop SensESP and PlatformIO leftovers ([#194](https://github.com/dirkwa/espos-p4-cockpit/issues/194)) ([54ec7ab](https://github.com/dirkwa/espos-p4-cockpit/commit/54ec7ab23ebb5507905380b918f38cccf270ce13))

## [1.5.6](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.5.5...v1.5.6) (2026-10-14)


### Fixed

* bump espOS to v0.19.0 ([#184](https://github.com/dirkwa/espos-p4-cockpit/issues/184)) ([66bdf20](https://github.com/dirkwa/espos-p4-cockpit/commit/66bdf209533d60e2ac1ab506613a389733ce4c75))

## [1.5.5](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.5.4...v1.5.5) (2026-10-13)


### Fixed

* bump espOS to v0.18.1 ([#181](https://github.com/dirkwa/espos-p4-cockpit/issues/181)) ([f1ad3d3](https://github.com/dirkwa/espos-p4-cockpit/commit/f1ad3d30cbf40c631631458836b48c0b62bff320))

## [1.5.4](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.5.3...v1.5.4) (2026-10-12)


### Fixed

* bump espOS to v0.17.3 ([#173](https://github.com/dirkwa/espos-p4-cockpit/issues/173)) ([164cf6e](https://github.com/dirkwa/espos-p4-cockpit/commit/164cf6e0f3bd427a07201add11b48a75c5e2ba7c))
* bump espOS to v0.18.0 ([#175](https://github.com/dirkwa/espos-p4-cockpit/issues/175)) ([d305afe](https://github.com/dirkwa/espos-p4-cockpit/commit/d305afe30b6872ac011b7e9b5406726849826205))

## [1.5.3](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.5.2...v1.5.3) (2026-10-11)


### Fixed

* bump espOS to v0.17.2 ([#171](https://github.com/dirkwa/espos-p4-cockpit/issues/171)) ([cfa5776](https://github.com/dirkwa/espos-p4-cockpit/commit/cfa57763c8206cf2d0734be92fe6c9ec708761c4))

## [1.5.2](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.5.1...v1.5.2) (2026-10-10)


### Fixed

* bump espOS to v0.16.1 ([#163](https://github.com/dirkwa/espos-p4-cockpit/issues/163)) ([edee521](https://github.com/dirkwa/espos-p4-cockpit/commit/edee52114668b5df8aa62a06d494c8dcee6c005f))

## [1.5.1](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.5.0...v1.5.1) (2026-10-10)


### Fixed

* bump espOS to v0.15.0 -- dependencies.lock not regenerated ([#159](https://github.com/dirkwa/espos-p4-cockpit/issues/159)) ([0efb433](https://github.com/dirkwa/espos-p4-cockpit/commit/0efb43304c8cb1c268cf41467627169937ba021d))
* bump espOS to v0.16.0 ([#161](https://github.com/dirkwa/espos-p4-cockpit/issues/161)) ([ee5a174](https://github.com/dirkwa/espos-p4-cockpit/commit/ee5a1749ca517e46966abdb5276d688e4e970718))

## [1.5.0](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.4.1...v1.5.0) (2026-10-03)


### ⚠ BREAKING CHANGES

* espOS v0.14.0, which moves the hosted transport to esp_hosted 3.x ([#153](https://github.com/dirkwa/espos-p4-cockpit/issues/153))

### Fixed

* **ci:** a breaking espOS release should open the tracking PR, not just fail ([#156](https://github.com/dirkwa/espos-p4-cockpit/issues/156)) ([43f788d](https://github.com/dirkwa/espos-p4-cockpit/commit/43f788d3f2cd4e03d1a0d97247e982a4ed7bb38d))


### Internal

* espOS v0.14.0, which moves the hosted transport to esp_hosted 3.x ([#153](https://github.com/dirkwa/espos-p4-cockpit/issues/153)) ([11b9fb5](https://github.com/dirkwa/espos-p4-cockpit/commit/11b9fb544a284b45a82b46149ff70440500ff788))
* release 1.5.0 rather than 2.0.0 for the espOS 3.x bump ([#157](https://github.com/dirkwa/espos-p4-cockpit/issues/157)) ([c284f9a](https://github.com/dirkwa/espos-p4-cockpit/commit/c284f9ab12506b0276bdfa2c14e153b804b20e94))

## [1.4.1](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.4.0...v1.4.1) (2026-09-29)


### Fixed

* bump espOS to v0.12.0 ([#148](https://github.com/dirkwa/espos-p4-cockpit/issues/148)) ([98f701f](https://github.com/dirkwa/espos-p4-cockpit/commit/98f701f765be1fdefdd8a07c4af445e43d1e3a4f))
* bump espOS to v0.12.1 ([#151](https://github.com/dirkwa/espos-p4-cockpit/issues/151)) ([eefe9ca](https://github.com/dirkwa/espos-p4-cockpit/commit/eefe9ca6ff2d5819ce976239055256dace0449cd))
* **ci:** keep the signing key out of PR builds, and dispatch the build that gets it ([#152](https://github.com/dirkwa/espos-p4-cockpit/issues/152)) ([06d4acb](https://github.com/dirkwa/espos-p4-cockpit/commit/06d4acb00189a53edefd096b6753ee96b255865b))
* **ci:** sign dispatched builds, because those are the ones we flash ([#150](https://github.com/dirkwa/espos-p4-cockpit/issues/150)) ([337da99](https://github.com/dirkwa/espos-p4-cockpit/commit/337da998697b8492f6f2a83b8d7c49b86d31bd5f))

## [1.4.0](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.3.2...v1.4.0) (2026-09-27)


### Added

* **release:** mirror the images where a browser can read them ([#143](https://github.com/dirkwa/espos-p4-cockpit/issues/143)) ([b2f7807](https://github.com/dirkwa/espos-p4-cockpit/commit/b2f780799caf5021553e3a462b862bf743f14a38))

## [1.3.2](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.3.1...v1.3.2) (2026-09-21)


### Fixed

* **ota:** an update no longer reboots the panel mid-download ([#139](https://github.com/dirkwa/espos-p4-cockpit/issues/139)) ([da1fcc3](https://github.com/dirkwa/espos-p4-cockpit/commit/da1fcc3af97842a894f0f140cf7500026df4fa1d))

## [1.3.1](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.3.0...v1.3.1) (2026-09-21)


### Fixed

* **jlp:** move the subject registry to LVGL's current subject API ([#134](https://github.com/dirkwa/espos-p4-cockpit/issues/134)) ([26a76a7](https://github.com/dirkwa/espos-p4-cockpit/commit/26a76a7e1cf1fb3800e964080d0382cad17a700c))

## [1.3.0](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.2.0...v1.3.0) (2026-09-20)


### Added

* **ci:** build and publish one image per panel board ([#131](https://github.com/dirkwa/espos-p4-cockpit/issues/131)) ([59c8203](https://github.com/dirkwa/espos-p4-cockpit/commit/59c820345f156ff540c74b9530a8f5afb4286fc6))

## [1.2.0](https://github.com/dirkwa/espos-p4-cockpit/compare/v1.1.0...v1.2.0) (2026-09-19)


### Added

* **slider:** [@brightness](https://github.com/brightness) sentinel for the backlight ([#126](https://github.com/dirkwa/espos-p4-cockpit/issues/126)) ([b2734c9](https://github.com/dirkwa/espos-p4-cockpit/commit/b2734c916bb6ca58a6678be09b41ab9fd1771c43))
* **theme:** apply layout-level theme to screen bg and widget accent ([#120](https://github.com/dirkwa/espos-p4-cockpit/issues/120)) ([14a9388](https://github.com/dirkwa/espos-p4-cockpit/commit/14a9388d57a6a19513d8fe9d3afa467093c5b3c2))


### Fixed

* **arc:** make tick marks visible, implement tick_labels ([#125](https://github.com/dirkwa/espos-p4-cockpit/issues/125)) ([8533ab4](https://github.com/dirkwa/espos-p4-cockpit/commit/8533ab4c65ba320eba9d1a0fa1318aa69a1fe605))
* **theme:** restore theme defaults when a layout build fails ([#124](https://github.com/dirkwa/espos-p4-cockpit/issues/124)) ([fa4636a](https://github.com/dirkwa/espos-p4-cockpit/commit/fa4636a296600fe74c8254dc165022d08d2fcbc2))

## Changelog

All notable changes to the P4 cockpit firmware. The format is
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are
semantic as judged from a panel's point of view: a *major* bump is where a
saved layout or the designer may have to change, a *minor* bump adds a widget
kind, field or capability, a *patch* fixes behaviour.

Entries name the area the way commit scopes do (`arc`, `theme`, `slider`,
…); the pull request number is the place to read the reasoning.

Sections are generated by release-please from the subjects of the commits on
main. The repo squash-merges with the pull request title as that subject, so
in practice the title is the entry, and one that is not a Conventional Commit
does not appear here.
