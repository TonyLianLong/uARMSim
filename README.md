# What's this?
This is simple ARM emulator called uARM, implemented by Dmitry Grinberg.
(http://dmitry.gr/index.php?r=05.Projects&proj=07.%20Linux%20on%208bit)

And it can run on the simulator now.

# How to compile

## get codes

```
$ git clone https://github.com/syuu1228/uARM.git
```

## compile

just run make.

```
$ make
```
Or, if your system installed BSD make, you'll need to run GNU make like this:
```
$ gmake
```

You will get ./uARM as an executable binary.

You can download the image from: http://pan.baidu.com/s/1sjp8KBN

Then uncompress the image.


## boot linux up.

```
$ ./uARM jaunty.rel.v2 fast_boot_file
```

You will get shell prompt after 10 seconds.

## enjoy in the sandbox.

You can terminate by using ```killall uARM``` on terminal.

You can find more information on: http://tonylianlong.com/
