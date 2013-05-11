APP = logtop
CFLAGS = -g3 -Wall
LIBS = -lcurses -lmenu

$(APP): main.c
	$(CC) $^ $(CFLAGS) $(LIBS) -o $(APP)

test: $(APP)
	./$(APP) test.config

clean:
	rm -fv $(APP)
