/*
 * wish.c — Wisconsin Shell
 * Laboratorio de Sistemas Operativos — Práctica No. 2
 * Universidad de Antioquia
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>

/* ──────────────────────────── Constantes ──────────────────────────── */
#define MAX_PATH_DIRS  64    /* máximo de directorios en el search path  */
#define MAX_ARGS       128   /* máximo de argumentos por comando         */
#define MAX_PARALLEL   64    /* máximo de comandos paralelos por línea   */

/* ──────────────────────────── Globales ────────────────────────────── */
char *search_path[MAX_PATH_DIRS];   /* directorios del search path        */
int   path_count = 0;               /* cantidad de directorios activos    */

/* Único mensaje de error permitido */
char error_message[30] = "An error has occurred\n";

/* ──────────────────────────── Utilidades ──────────────────────────── */

/**
 * print_error – imprime el mensaje de error estándar a stderr.
 */
void print_error(void) {
    write(STDERR_FILENO, error_message, strlen(error_message));
}

/**
 * trim – elimina espacios/tabulaciones al inicio y al final de la cadena.
 * Retorna un puntero al primer carácter no-blanco (dentro del mismo buffer).
 */
char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    int len = (int)strlen(s);
    while (len > 0 &&
           (s[len-1] == ' ' || s[len-1] == '\t' ||
            s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
    return s;
}

/* ─────────────────────────── Search path ──────────────────────────── */

void free_path(void) {
    for (int i = 0; i < path_count; i++) {
        free(search_path[i]);
        search_path[i] = NULL;
    }
    path_count = 0;
}

void init_path(void) {
    search_path[0] = strdup("/bin");
    path_count = 1;
}

char *find_executable(const char *cmd) {
    char buf[1024];
    for (int i = 0; i < path_count; i++) {
        snprintf(buf, sizeof(buf), "%s/%s", search_path[i], cmd);
        if (access(buf, X_OK) == 0) {
            return strdup(buf);
        }
    }
    return NULL;
}

/* ──────────────────────── Ejecución de un comando ─────────────────── */

pid_t run_command(char *segment) {

    /* 1. Limpiar espacios */
    segment = trim(segment);
    if (strlen(segment) == 0) return -1;   /* línea vacía, sin error */

    /* ── 2. Detectar redirección ── */
    char *redir_file = NULL;

    int redir_count = 0;
    for (int i = 0; segment[i] != '\0'; i++) {
        if (segment[i] == '>') redir_count++;
    }
    if (redir_count > 1) {
        print_error();
        return -1;
    }

    char *redir_pos = strchr(segment, '>');
    if (redir_pos != NULL) {
        *redir_pos = '\0';              /* cortar la cadena en '>'      */
        char *after = trim(redir_pos + 1);

        /* Debe haber exactamente un nombre de archivo */
        if (strlen(after) == 0) {
            print_error();
            return -1;
        }

        /* Verificar que no haya múltiples tokens (múltiples archivos) */
        char tmp[4096];
        strncpy(tmp, after, sizeof(tmp) - 1);
        tmp[sizeof(tmp)-1] = '\0';
        char *p   = tmp;
        char *tok;
        int   cnt = 0;
        while ((tok = strsep(&p, " \t")) != NULL) {
            if (strlen(tok) > 0) cnt++;
        }
        if (cnt != 1) {
            print_error();
            return -1;
        }
        redir_file = after;             /* puntero dentro de 'segment'  */
    }

    /* ── 3. Parsear argumentos ── */
    char *args[MAX_ARGS + 1];
    int   argc = 0;
    char *rest = segment;
    char *token;

    while ((token = strsep(&rest, " \t")) != NULL) {
        if (strlen(token) > 0) {
            if (argc < MAX_ARGS)
                args[argc++] = token;
        }
    }
    args[argc] = NULL;

    /* Validación de caso borde: redirección sin comando (Ej: "> archivo.txt") */
    if (argc == 0) {
        if (redir_file != NULL) print_error();
        return -1;
    }

    /* ── 4. Built-in: exit ── */
    if (strcmp(args[0], "exit") == 0) {
        if (argc != 1) {
            print_error();
            return -1;
        }
        free_path(); /* Limpieza de memoria antes de salir */
        exit(0);
    }

    /* ── 5. Built-in: cd / chd ── */
    /* Soporte dual por inconsistencia en el documento del laboratorio */
    if (strcmp(args[0], "chd") == 0 || strcmp(args[0], "cd") == 0) {
        if (argc != 2) {
            print_error();
            return -1;
        }
        if (chdir(args[1]) != 0) {
            print_error();
        }
        return -1;              /* built-in: sin fork                 */
    }

    /* ── 6. Built-in: route / path ── */
    /* Soporte dual por inconsistencia en el documento del laboratorio */
    if (strcmp(args[0], "route") == 0 || strcmp(args[0], "path") == 0) {
        free_path();
        for (int i = 1; i < argc; i++) {
            if (path_count < MAX_PATH_DIRS)
                search_path[path_count++] = strdup(args[i]);
        }
        return -1;              /* built-in: sin fork                 */
    }

    /* ── 7. Comando externo ── */
    char *exec_path = find_executable(args[0]);
    if (exec_path == NULL) {
        print_error();
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        print_error();
        free(exec_path);
        return -1;
    }

    if (pid == 0) {
        /* ── Proceso hijo ── */
        if (redir_file != NULL) {
            int fd = open(redir_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                print_error();
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);   /* redirigir stdout */
            dup2(fd, STDERR_FILENO);   /* redirigir stderr (el "giro" del lab) */
            close(fd);
        }
        execv(exec_path, args);
        /* Si execv retorna, hubo un error */
        print_error();
        free(exec_path);
        exit(1);
    }

    /* Proceso padre: liberar y retornar PID */
    free(exec_path);
    return pid;
}

/* ─────────────────────────── Procesar línea ───────────────────────── */

void process_line(char *line) {
    /* Eliminar salto de línea */
    line[strcspn(line, "\n")] = '\0';

    /* ── Dividir por '&' ── */
    char *segments[MAX_PARALLEL];
    int   num_segs = 0;
    char *rest     = line;
    char *seg;

    while ((seg = strsep(&rest, "&")) != NULL) {
        if (num_segs < MAX_PARALLEL)
            segments[num_segs++] = seg;
    }

    /* ── Lanzar cada segmento ── */
    pid_t pids[MAX_PARALLEL];
    int   num_pids = 0;

    for (int i = 0; i < num_segs; i++) {
        char seg_copy[4096];
        strncpy(seg_copy, segments[i], sizeof(seg_copy) - 1);
        seg_copy[sizeof(seg_copy)-1] = '\0';

        pid_t pid = run_command(seg_copy);
        if (pid > 0) {
            pids[num_pids++] = pid;
        }
    }

    /* ── Esperar todos los hijos ── */
    for (int i = 0; i < num_pids; i++) {
        waitpid(pids[i], NULL, 0);
    }
}

/* ──────────────────────────────── main ────────────────────────────── */

int main(int argc, char *argv[]) {
    init_path();

    if (argc > 2) {
        print_error();
        exit(1);
    }

    FILE *input;
    int   interactive;

    if (argc == 2) {
        input = fopen(argv[1], "r");
        if (input == NULL) {
            print_error();
            exit(1);
        }
        interactive = 0;
    } else {
        input       = stdin;
        interactive = 1;
    }

    char  *line = NULL;
    size_t len  = 0;

    while (1) {
        if (interactive) {
            printf("wish> ");
            fflush(stdout);
        }

        ssize_t nread = getline(&line, &len, input);
        if (nread == -1) {
            /* EOF → salir limpiamente */
            free(line);
            free_path();
            if (!interactive) fclose(input);
            exit(0);
        }

        process_line(line);
    }

    return 0;
}
