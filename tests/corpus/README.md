# Test corpus

Two short replays from conforming producers, each as a `.usdc` file and as the usd-web bundle made from it. Both
producers are MIT-licensed. CI opens both forms, checks that they give the same model (`replay-compare`), draws them
at fixed ticks and compares the images with `tests/golden/` (`strata-imgcmp`), and plays the bundles in the browser
smoke test.

| Files | Made with |
|---|---|
| `steading-seed42.usdc`, `steading-seed42/` | [steading](https://github.com/Preponderous-Software/steading) at `3067c47c06221e44cbc8defdc12d959ae75b8eed`: `steading-replay --new 42 --ticks 120 --every 4 --version 0.1.0+3067c47c0622 --out steading-seed42.usda`; last state hash `1cfb364224b7b6ee` |
| `kindling-restoration.usdc`, `kindling-restoration/` | [Kindling](https://github.com/Preponderous-Software/kindling) at `63e9bd4a7eca7f23d113d56fc28b5150d126c9ab`: `kindling --headless --autopilot --replay restoration.usda`; last state hash `0b008efbdb48bfa0` |

Then, with usd-recorder and usd-web (0.2.1):

```sh
usd-recorder replay convert NAME.usda NAME.usdc
usd-web replay NAME.usdc -o NAME --chunk-kb 64
```

`tests/data/scenario.usda` is usd-recorder's own replay scenario (`tests/replay_scenario.py` there, the Python writer);
`tests/data/scenario/` is its bundle in four small chunks and `scenario-every3/` keeps one sample in three.
