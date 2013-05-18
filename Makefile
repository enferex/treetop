APP = treetop
CFLAGS = -g3 -Wall
LIBS = -lcurses -lmenu -lpanel

$(APP): main.c
	$(CC) $^ $(CFLAGS) $(LIBS) -o $(APP)

test: $(APP)
	./$(APP) test.config

debug: $(APP)
	exec gdb --args ./$(APP) test.config

clean:
	rm -fv $(APP)
