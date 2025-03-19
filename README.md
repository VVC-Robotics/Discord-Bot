# Discord-Bot

Simple Discord bot for the club Discord server.

Handles new users and prompts them with a verification dialog.

### How to use

Install DPP

`git clone https://github.com/brainboxdotcc/DPP && cd DPP && mkdir build && cmake .. && make -j8 && sudo make install && cd ../../`

1. Download repository

```
git clone https://github.com/VVC-Robotics/Discord-Bot
cd Discord-Bot
```

2. Place bot token in a file called `TOKEN` 

```
echo -n "YOUR TOKEN HERE" > TOKEN
```

3. Compile

```
mkdir build && cd build
cmake .. && make
```

4. Run

```
./Discord-Bot
```

### To-Do

- [ ] Cache the new role that is created
- [ ] Dialog to add a student's email to a mailing list
- [ ] Add DPP as submodule to fix instructions