#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE	// для getline

#include<ctype.h>
#include<stdio.h>
#include<errno.h>
#include<sys/ioctl.h>	//для размера консоли
#include<sys/types.h>	//для ssize_t
#include<termios.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>

/*** defines ***/

#define KHUD_VERSION "0.0.1"	//для вывода версии
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

/*** data ***/

//тип данных для хранения строки
typedef struct erow{
	int size;
	char* chars;
} erow;

struct editorConfig{
	int cursorX, cursorY;	//координаты курсора
	int rowoff;		//смещение строки
	int coloff;		//смещение столбца
	int screenrows;
	int screencols;
	int numrows;
	erow* row;
	struct termios orig_termios;	//оригинальные настройки терминала
};

struct editorConfig E;

/*** terminal ***/
void die(const char* s){
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}

void disableRawMode(){
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

//тут мы убираем все флаги для получения сырого терминала
void enableRawMode(){
		
	if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
		die("tcgetattr");
	atexit(disableRawMode);	//исполняется при выходе, возвращаем дефолт настройки терминала

	struct termios raw = E.orig_termios;
	//хуйни для отключения флагов, нп. управление потоком
	raw.c_oflag &= ~(OPOST);	//офает \r
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_cflag |= (CS8);	//битовая маска 
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("tcsetattr");
}

int editorReadKey(){
	int nread;
	char c;
	while((nread = read(STDIN_FILENO, &c, 1)) != 1){
		if(nread == -1 && errno != EAGAIN) die("read");
	}
	//если это управляющий символ 
	if(c == '\x1b'){
		char seq[3];
		if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
	
		if(seq[0] == '['){
			if(seq[1] >= '0' && seq[1] <= '9'){
				if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if(seq[2] == '~'){
					switch(seq[1]){
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}		
			
			
			}
			else{
				switch(seq[1]){
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		}
		else if(seq[0] == 'O'){
			switch(seq[1]){
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}
		return '\x1b';
	}
	else{
		return c;
	}
	
}
//находим позицию курсора
int getCursorPosition(int* rows, int* cols){
	char buf[32];
	unsigned int i = 0; 
	
	if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
	
	while(i < sizeof(buf) - 1){
		if(read(STDOUT_FILENO, &buf[i], 1) != 1) break;
		if(buf[i] == 'R') break;
		++i;
	}
	buf[i] = '\0';
	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  	return 0;
}

int getWindowSize(int* rows, int* cols){
	struct winsize ws;	//ioctl библиотека, помимо размеров окна есть и другие приколы
	
	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1  
	|| ws.ws_col == 0){
		if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		
		return getCursorPosition(rows, cols);		
	}
	else{
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/*** row operations ***/

void editorAppendRow(char* s, size_t len){
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';
	E.numrows++;
}

/*** file i/o ***/

void editorOpen(char* filename){
	FILE* fp = fopen(filename, "r");
	if(!fp) die("fopen");

	char* line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	
	while((linelen = getline(&line, &linecap, fp)) != -1){
		while(linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			--linelen;
		editorAppendRow(line, linelen);
	}
	free(line);
	fclose(fp);
	
}


/*** append buffer ***/

struct abuf{
	char* b;
	int len;
};
#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf* ab, const char* s, int len){
	char* new = realloc(ab->b, ab->len + len);
	
	if(new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf* ab){
	free(ab->b);
}

/*** output ***/

void editorScroll(){
	if(E.cursorY < E.rowoff){
		E.rowoff = E.cursorY;
	}
	if(E.cursorY >= E.rowoff + E.screenrows){
		E.rowoff = E.cursorY - E.screenrows + 1;
	}
	if(E.cursorX < E.coloff + E.screencols){
		E.coloff = E.cursorX;
	}
	if(E.cursorX >= E.coloff + E.screencols){
		E.coloff = E.cursorX - E.screencols + 1;
	}
}

//вывод 
void editorDrawRows(struct abuf* ab){
	for(int i = 0; i < E.screenrows; ++i){
		int filerow = i + E.rowoff;
		if(filerow >= E.numrows){
			if(E.numrows == 0 && i == E.screenrows / 3){
				char welcome[80];

				int welcomeLength = snprintf(welcome, sizeof(welcome),
				"Khud editor -- version %s", KHUD_VERSION);

				if(welcomeLength > E.screencols) welcomeLength = E.screencols;
				int padding = (E.screencols - welcomeLength) / 2;
				if(padding){
					abAppend(ab, "~", 1);
					--padding;
				}
				while(padding--) abAppend(ab, " ", 1);
				abAppend(ab, welcome, welcomeLength);
			}
			else{
				abAppend(ab, "~", 1);
			}
		}
		else{
			int len = E.row[filerow].size - E.coloff;
			if(len < 0) len = 0;
			if(len > E.screencols) len = E.screencols;
			abAppend(ab, &E.row[filerow].chars[E.coloff], len);
		}
		
		abAppend(ab, "\x1b[K", 3);  //K удаляет часть строки	

		if(i < E.screenrows - 1){
			abAppend(ab, "\r\n", 2);
		}
	}
}

void editorRefreshScreen(){
	editorScroll();

	struct abuf ab = ABUF_INIT;
	
	abAppend(&ab, "\x1b[?25l", 6);	//скрыть курсор
	abAppend(&ab, "\x1b[H", 3);	//чтобы курсор был сверху
	
	editorDrawRows(&ab);
	
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cursorY - E.rowoff) + 1, (E.cursorX - E.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);	//показать курсор

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/*** input ***/

//отвечает за движение курсора
void editorMoveCursor(int key){
	erow* row = (E.cursorY >= E.numrows) ? NULL : &E.row[E.cursorY];
	switch(key){
		case ARROW_LEFT:
			if(E.cursorX != 0) E.cursorX--;
			//для перевода на предыдущую строку
			else if(E.cursorY > 0){
				E.cursorY--;
				E.cursorX = E.row[E.cursorY].size;
			}
			break;

		case ARROW_RIGHT:
			if(row && E.cursorX < row->size) E.cursorX++;
			//для перевода на следующую строку
			else if(row && E.cursorX == row->size){
				E.cursorY++;
				E.cursorX = 0;
			}		
			break;

		case ARROW_UP:
			if(E.cursorY != 0)E.cursorY--;
			break;

		case ARROW_DOWN:
			if(E.cursorY < E.numrows) E.cursorY++;
			break;	
	}

	row = (E.cursorY >= E.numrows) ? NULL : &E.row[E.cursorY];
	int rowlen = row ? row->size : 0;
	if(E.cursorX > rowlen){
		E.cursorX = rowlen;
	}
}

void editorProcessKeypress(){
	int c = editorReadKey();

	switch(c){
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);

			exit(0);
			break;
		
		case HOME_KEY:
			E.cursorX = 0;
			break;
		case END_KEY:
			E.cursorX = E.screencols - 1;
			break;		

		case PAGE_UP:
		case PAGE_DOWN:
			{
				int times = E.screenrows;
				while(times--)
					editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;

		case ARROW_UP:
		case ARROW_LEFT:
		case ARROW_DOWN:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
	}
}

/*** init ***/

void initEditor(){
	E.cursorX = 0;
	E.cursorY = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;
	if(getWindowSize(&E.screenrows, &E.screencols) == -1){
		die("getWindowSize");
	}
}

int main(int argc, char* argv[]){
	enableRawMode();	
	initEditor();
	if(argc >= 2){
		editorOpen(argv[1]);
	}	
	

	while(1){
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}
