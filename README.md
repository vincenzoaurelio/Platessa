# Platessa
![platessa](logo.png)

platessa.py is a markdown viewer vibecoded in python with Tkinter.

## Features
- Pictures displayed in place
- LaTeX formulas renderer

## Install requirements
If you don't want to create a virtual environment skip to second step 

1. Set python venv 
    ```bash
    python -m venv platessa
    source platessa/bin/activate
    ```
2. Install requirements
    ```bash
    pip install -r requirements.txt
    ```

## Usage
- Press C-o to open a file or press `:` and then type the path
- Vim keybindings: j k h l gg G C-d C-u C-f C-b
- Zoom: + / - / 0 with automatic window resize
- Support for local (relative/absolute) and remote images
- Clicking links: images displayed in place, other links copied to clipboard
- Reload file: r
- Quit reader: q
