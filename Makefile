APP = logtop
CFLAGS = -g3 -Wall
LIBS = -lcurses -lmenu

$(APP): main.c
	$(CC) $^ $(CFLAGS) $(LIBS) -o $(APP)

clean:
	rm -fv $(APP)
