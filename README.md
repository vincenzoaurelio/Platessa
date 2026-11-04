# Platessa
platessa.py is a markdown viewer vibecoded in python with Tkinter.

## Install requirements
If you don't want to create a virtual environment skip to second step 

1. Set python venv 
    ```bash
    python -m venv platessa
    source platessa/bin/activate
    ```
2. Install requirements
    `pip install requirements.txt`

## Usage
- Vim keybindings: j k h l gg G C-d C-u C-f C-b
- Zoom: + / - / 0 with automatic window resize
- Support for local (relative/absolute) and remote images
- Clicking links: images displayed in place, other links copied to clipboard
- Reload file: r
- Quit reader: q
