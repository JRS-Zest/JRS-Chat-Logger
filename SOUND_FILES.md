# Sound Files

This project loads notification sounds from the `sounds` folder placed next to the executable.

## Required file names and usage

- `whisper.wav`: whisper receive notification
- `item.wav`: item pickup notification
- `option.wav`: option match notification
- `inv_full.wav`: inventory full notification
- `bell.wav`: bell event notification
- `Ultimate.wav`: drop notification

## Location

- Runtime path: `<exe directory>/sounds/`

## Notes

- File names are case-sensitive in source code expectations. Keep exact names.
- If a file is missing, that notification sound is skipped.
