#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <jansson.h>  // Biblioteca para manejar JSON

#define MAX_USERS 1000
#define MAX_OPERATIONS 1000

typedef struct {
    int no_cuenta;
    char nombre[100];
    double saldo;
} Usuario;

typedef struct {
    int operacion; // 1: Deposito, 2: Retiro, 3: Transferencia
    int cuenta1;
    int cuenta2; // Solo usado en transferencias
    double monto;
} Operacion;

typedef struct {
    int hilos[3];
    char errores[1000][100];
} ReporteUsuarios;

int errorCargaU = 0;
ReporteUsuarios r1 = { .hilos = {0, 0, 0}, .errores = {0} };

typedef struct {
    int hilos[4];
    int operaciones[3];
    char errores[1000][100];
} ReporteOperaciones;

int errorCargaO = 0;
ReporteOperaciones r2 = { .hilos = {0, 0, 0, 0}, .operaciones = {0, 0, 0}, .errores = {0} };

Usuario usuarios[MAX_USERS];
int num_usuarios = 0;
int lineas_usuarios = 0;
int lineas_operaciones = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Declaraciones de funciones
char* fecha_hora(char* formato);
void* cargar_usuarios(void* filename);
void* cargar_operaciones(void* filename);
void deposito(int no_cuenta, double monto, int index, int esCarga);
void retiro(int no_cuenta, double monto, int index, int esCarga);
void transferencia(int cuenta_origen, int cuenta_destino, double monto, int index, int esCarga);
void consultar_cuenta(int no_cuenta);

// Función para formatear fecha y hora
char* fecha_hora(char* formato) {
    static char datetime[29];
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    strftime(datetime, sizeof(datetime), formato, local);
    return datetime;
}

// Función para cargar usuarios desde un archivo JSON
void* cargar_usuarios(void* i) {
    int n = *((int*) i);
    char filename[100];
    sprintf(filename, "usuarios/usuarios%d.json", n + 1);

    const char* file = filename;
    json_error_t error;
    json_t *root = json_load_file(file, 0, &error);
    if (!root) {
        fprintf(stderr, "Error al cargar JSON: %s\n", error.text);
        return NULL;
    }

    size_t index;
    json_t *user;
    json_array_foreach(root, index, user) {
        pthread_mutex_lock(&mutex);
        int no_cuenta = json_integer_value(json_object_get(user, "no_cuenta"));
        const char* nombre = json_string_value(json_object_get(user, "nombre"));
        double saldo = json_real_value(json_object_get(user, "saldo"));

        lineas_usuarios++;

        // Verificar si el número de cuenta ya existe
        int i;
        for (i = 0; i < num_usuarios; ++i) {
            if (usuarios[i].no_cuenta == no_cuenta) {
                sprintf(r1.errores[errorCargaU], "    - Linea #%d: Número de cuenta duplicado %d", lineas_usuarios, no_cuenta);
                errorCargaU++;
                pthread_mutex_unlock(&mutex);
                continue;
            }
        }
        // Verificar si el número de cuenta o el saldo son negativos
        if(no_cuenta < 0) {
            sprintf(r1.errores[errorCargaU], "    - Linea #%d: Número de cuenta no puede ser menor que 0", lineas_usuarios);
            errorCargaU++;
            pthread_mutex_unlock(&mutex);
            continue;
        } else if(saldo < 0) {
            sprintf(r1.errores[errorCargaU], "    - Linea #%d: Saldo no puede ser menor que 0", lineas_usuarios);
            errorCargaU++;
            pthread_mutex_unlock(&mutex);
            continue;
        }

        // Agregar usuario al arreglo
        usuarios[num_usuarios].no_cuenta = no_cuenta;
        strncpy(usuarios[num_usuarios].nombre, nombre, sizeof(usuarios[num_usuarios].nombre) - 1);
        usuarios[num_usuarios].saldo = saldo;
        num_usuarios++;
        r1.hilos[n]++;
        pthread_mutex_unlock(&mutex);
    }
    json_decref(root);
    return NULL;
}

// Función para realizar operaciones desde un archivo JSON
void* cargar_operaciones(void* i) {
    int n = *((int*) i);
    char filename[100];
    sprintf(filename, "operaciones/operaciones%d.json", n + 1);
    // printf("%s\n", filename);

    const char* file = filename;
    json_error_t error;
    json_t *root = json_load_file(file, 0, &error);
    if (!root) {
        fprintf(stderr, "Error al cargar JSON: %s\n", error.text);
        return NULL;
    }

    size_t index;
    json_t *operation;
    json_array_foreach(root, index, operation) {
        int operacion = json_integer_value(json_object_get(operation, "operacion"));
        int cuenta1 = json_integer_value(json_object_get(operation, "cuenta1"));
        int cuenta2 = json_integer_value(json_object_get(operation, "cuenta2"));
        double monto = json_real_value(json_object_get(operation, "monto"));

        lineas_operaciones ++;

        switch (operacion) {
            case 1:
                deposito(cuenta1, monto, n, 1);
                break;
            case 2:
                retiro(cuenta1, monto, n, 1);
                break;
            case 3:
                transferencia(cuenta1, cuenta2, monto, n, 1);
                break;
            default:
                printf("Operación desconocida: %d\n", operacion);
                break;
        }
    }
    json_decref(root);
    // printf("termina: %s\n", filename);
    return NULL;
}

// Funciones para las operaciones individuales
void deposito(int no_cuenta, double monto, int index, int esCarga) {
    if (monto <= 0) {
        if(esCarga) {
            sprintf(r2.errores[errorCargaO], "    - Linea #%d: Monto no válido para depósito: %.2f", lineas_operaciones, monto);
            errorCargaO ++;
        } else {
            printf("\x1b[31m" "Monto no válido para depósito: %.2f\n" "\x1b[0m", monto);
        }
        return;
    }
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < num_usuarios; ++i) {
        if (usuarios[i].no_cuenta == no_cuenta) {
            usuarios[i].saldo += monto;
            if(esCarga) {
                r2.operaciones[1]++;
                r2.hilos[index]++;
            } else {
                printf("\x1b[32m" "Depósito exitoso. Nuevo saldo de la cuenta %d: %.2f\n" "\x1b[0m", no_cuenta, usuarios[i].saldo);
            }
            pthread_mutex_unlock(&mutex);
            return;
        }
    }
    if(esCarga) {
        sprintf(r2.errores[errorCargaO], "    - Linea #%d: Número de cuenta no encontrado: %d", lineas_operaciones, no_cuenta);
        errorCargaO ++;
    } else {
        printf("\x1b[31m" "Número de cuenta no encontrado: %d\n" "\x1b[0m", no_cuenta);
    }
    pthread_mutex_unlock(&mutex);
}

void retiro(int no_cuenta, double monto, int index, int esCarga) {
    if (monto <= 0) {
        if(esCarga) {
            sprintf(r2.errores[errorCargaO], "    - Linea #%d: Monto no válido para retiro: %.2f", lineas_operaciones, monto);
            errorCargaO ++;
        } else {
            printf("\x1b[31m" "Monto no válido para retiro: %.2f\n" "\x1b[0m", monto);
        }
        return;
    }
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < num_usuarios; ++i) {
        if (usuarios[i].no_cuenta == no_cuenta) {
            if (usuarios[i].saldo >= monto) {
                usuarios[i].saldo -= monto;
                if(esCarga) {
                    r2.operaciones[0]++;
                    r2.hilos[index]++;
                    lineas_operaciones++;
                } else {
                    printf("\x1b[32m" "Retiro exitoso. Nuevo saldo de la cuenta %d: %.2f\n" "\x1b[0m", no_cuenta, usuarios[i].saldo);
                }
            } else {
                if(esCarga) {
                    sprintf(r2.errores[errorCargaO], "    - Linea #%d: Saldo insuficiente en la cuenta %d", lineas_operaciones, no_cuenta);
                    errorCargaO ++;
                } else {
                    printf("\x1b[31m" "Saldo insuficiente en la cuenta %d\n" "\x1b[0m", no_cuenta);
                }
            }
            pthread_mutex_unlock(&mutex);
            return;
        }
    }
    if(esCarga) {
        sprintf(r2.errores[errorCargaO], "    - Linea #%d: Número de cuenta no encontrado: %d", lineas_operaciones, no_cuenta);
        errorCargaO ++;
    } else {
        printf("\x1b[31m" "Número de cuenta no encontrado: %d\n" "\x1b[0m", no_cuenta);
    }
    pthread_mutex_unlock(&mutex);
}

void transferencia(int cuenta_origen, int cuenta_destino, double monto, int index, int esCarga) {
    if (monto <= 0) {
        if(esCarga) {
            sprintf(r2.errores[errorCargaO], "    - Linea #%d: Monto no válido para transferencia: %.2f", lineas_operaciones, monto);
            errorCargaO ++;
        } else {
            printf("\x1b[31m" "Monto no válido para transferencia: %.2f\n" "\x1b[0m", monto);
        }
        return;
    }
    pthread_mutex_lock(&mutex);
    Usuario *origen = NULL, *destino = NULL;
    for (int i = 0; i < num_usuarios; ++i) {
        if (usuarios[i].no_cuenta == cuenta_origen) {
            origen = &usuarios[i];
        }
        if (usuarios[i].no_cuenta == cuenta_destino) {
            destino = &usuarios[i];
        }
    }
    if (origen && destino) {
        if (origen->saldo >= monto) {
            origen->saldo -= monto;
            destino->saldo += monto;
            if(esCarga) {
                r2.operaciones[2]++;
                r2.hilos[index]++;
            } else {
                printf("\x1b[32m" "Transferencia exitosa de la cuenta %d a la cuenta %d por %.2f\n" "\x1b[0m", cuenta_origen, cuenta_destino, monto);
            }
        } else {
            if(esCarga) {
                sprintf(r2.errores[errorCargaO], "    - Linea #%d: Saldo insuficiente en la cuenta origen %d", lineas_operaciones, cuenta_origen);
                errorCargaO ++;
            } else {
                printf("\x1b[31m" "Saldo insuficiente en la cuenta %d\n" "\x1b[0m", cuenta_origen);
            }
        }
    } else {
        if (!origen) {
            if(esCarga) {
                sprintf(r2.errores[errorCargaO], "    - Linea #%d: Número de cuenta de origen no encontrado: %d", lineas_operaciones, cuenta_origen);
                errorCargaO ++;
            } else {
                printf("\x1b[31m" "Número de cuenta de origen no encontrado: %d\n" "\x1b[0m", cuenta_origen);
            }
        }
        if (!destino) {
            if(esCarga) {
                sprintf(r2.errores[errorCargaO], "    - Linea #%d: Número de cuenta de destino no encontrado: %d", lineas_operaciones, cuenta_destino);
                errorCargaO ++;
            } else {
                printf("\x1b[31m" "Número de cuenta de destino no encontrado: %d\n" "\x1b[0m", cuenta_destino);
            }
        }
    }
    pthread_mutex_unlock(&mutex);
}

void consultar_cuenta(int no_cuenta) {
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < num_usuarios; ++i) {
        if (usuarios[i].no_cuenta == no_cuenta) {
            printf("Número de cuenta: %d\nNombre: %s\nSaldo: %.2f\n", usuarios[i].no_cuenta, usuarios[i].nombre, usuarios[i].saldo);
            pthread_mutex_unlock(&mutex);
            return;
        }
    }
    printf("\x1b[31m" "Número de cuenta no encontrado: %d\n" "\x1b[0m", no_cuenta);
    pthread_mutex_unlock(&mutex);
}

void reporteCargaUsuarios() {
    char nombre[29];
    sprintf(nombre, "carga_%s.log", fecha_hora("%Y_%m_%d-%H_%M_%S"));
    char fecha[19];
    sprintf(fecha, "%s", fecha_hora("%Y-%m-%d %H:%M:%S"));
    // abrir o crear archivo
    int fd = open(nombre, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        exit(1);
    }
    //
    int total = 0;
    char buffer[4096 * 2];
    int offset = snprintf(buffer, sizeof(buffer), "-------------- Carga de Usuarios --------------\n\nFecha: %s\n\nUsuarios Cargados:\n", fecha);
    for (int i = 0; i < 3; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "Hilo #%d: %d\n", i + 1, r1.hilos[i]);
        total += r1.hilos[i];
    }
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "Total: %d\n\nErrores:\n", total);
    for (int i = 0; i < 1000 && r1.errores[i][0] != '\0'; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s\n", r1.errores[i]);
    }

    // Escribir el contenido del buffer en el archivo
    write(fd, buffer, strlen(buffer));

    close(fd);
    //
    printf("\x1b[32m" "¡Reporte de Carga de Usuarios generado!\n\n" "\x1b[0m");
}


void reporteCargaOperaciones() {
    char nombre[35];
    sprintf(nombre, "operaciones_%s.log", fecha_hora("%Y_%m_%d-%H_%M_%S"));
    char fecha[19];
    sprintf(fecha, "%s", fecha_hora("%Y-%m-%d %H:%M:%S"));
    // abrir o crear archivo
    int fd = open(nombre, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        exit(1);
    }
    //
    int total = 0;
    char buffer[4096 * 2];
    int offset = snprintf(buffer, sizeof(buffer), "-------------- Resumen de Operaciones --------------\n\nFecha: %s\n\nOperaciones realizadas:\n", fecha);
    for (int i = 0; i < 3; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, i == 0 ? "Retiros: %d\n" : (i == 1 ? "Depositos: %d\n" : "Transferencias: %d\n"), r2.operaciones[i]);
        total += r2.operaciones[i];
    }
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "Total: %d\n\nOperaciones por hilo:\n", total);
    total = 0;
    for (int i = 0; i < 4; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "Hilo #%d: %d\n", i + 1, r2.hilos[i]);
        total += r2.hilos[i];
    }
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "Total: %d\n\nErrores:\n", total);
    for (int i = 0; i < 1000 && r2.errores[i][0] != '\0'; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s\n", r2.errores[i]);
    }

    // Escribir el contenido del buffer en el archivo
    write(fd, buffer, strlen(buffer));

    close(fd);
    //
    printf("\x1b[32m" "¡Reporte de Carga de Operaciones generado!\n\n" "\x1b[0m");
}

void reporteEstadosCuenta() {
    int fd = open("estado_cuenta.json", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        exit(1);
    }
    //
    json_t *root = json_array();
    if (!root) {
        perror("json_array");
        close(fd);
        exit(1);
    }

    // Agregar cada usuario al arreglo JSON
    for (int i = 0; i < MAX_USERS; i++) {
        if (usuarios[i].no_cuenta != 0) {  // Verificar si el usuario es válido
            json_t *usuario = json_object();
            json_object_set_new(usuario, "no_cuenta", json_integer(usuarios[i].no_cuenta));
            json_object_set_new(usuario, "nombre", json_string(usuarios[i].nombre));
            json_object_set_new(usuario, "saldo", json_real(usuarios[i].saldo));
            json_array_append_new(root, usuario);
        }
    }

    // Escribir el objeto JSON en el archivo
    char *json_str = json_dumps(root, JSON_INDENT(4));
    if (!json_str) {
        perror("json_dumps");
        json_decref(root);
        close(fd);
        exit(1);
    }

    if (write(fd, json_str, strlen(json_str)) == -1) {
        perror("write");
        free(json_str);
        json_decref(root);
        close(fd);
        exit(1);
    }

    free(json_str);
    json_decref(root);
    close(fd);
    //
    close(fd);
    //
    printf("\x1b[32m" "¡Estados de Cuenta generado!\n\n" "\x1b[0m");
}

int main() {
    pthread_mutex_init(&mutex, NULL);

    pthread_t threads[7];

    // Menú de operaciones
    int opcion;
    while (1) {
        printf("Menú de Operaciones:\n1. Deposito\n2. Retiro\n3. Transferencia\n4. Consultar cuenta\n5. Cargar Usuarios\n6. Cargar Operaciones\n7. Generar Estados de Cuenta\n8. Salir\n\nOpción: ");
        scanf("%d", &opcion);

        int no_cuenta, cuenta_origen, cuenta_destino;
        double monto;
        int index[4];

        switch (opcion) {
            case 1:
                printf("\n\x1b[36mDepósito\x1b[0m\nNúmero de cuenta: ");
                scanf("%d", &no_cuenta);
                printf("Monto a depositar: ");
                scanf("%lf", &monto);
                deposito(no_cuenta, monto, -1, 0);
                printf("\n");
                break;
            case 2:
                printf("\n\x1b[36mRetiro\x1b[0m\nNúmero de cuenta: ");
                scanf("%d", &no_cuenta);
                printf("Monto a retirar: ");
                scanf("%lf", &monto);
                retiro(no_cuenta, monto, -1, 0);
                printf("\n");
                break;
            case 3:
                printf("\n\x1b[36mTransferencia\x1b[0m\nNúmero de cuenta origen: ");
                scanf("%d", &cuenta_origen);
                printf("Número de cuenta destino: ");
                scanf("%d", &cuenta_destino);
                printf("Monto a transferir: ");
                scanf("%lf", &monto);
                transferencia(cuenta_origen, cuenta_destino, monto, -1, 0);
                printf("\n");
                break;
            case 4:
                printf("\n\x1b[36mConsulta de Cuenta\x1b[0m\nNúmero de cuenta: ");
                scanf("%d", &no_cuenta);
                consultar_cuenta(no_cuenta);
                printf("\n");
                break;
            case 5:
                printf("\x1b[32m" "\nCarga de Usuarios...\n" "\x1b[0m");
                // Cargar usuarios en 3 hilos
                for (int i = 0; i < 3; i++) {
                    index[i] = i;
                    pthread_create(&threads[i], NULL, cargar_usuarios, &index[i]);
                }

                for (int i = 0; i < 3; i++) {
                    pthread_join(threads[i], NULL);
                }
                printf("\x1b[32m" "¡Carga de Usuarios Finalizada!\n" "\x1b[0m");
                reporteCargaUsuarios();
                break;
            case 6:
                printf("\x1b[32m" "\nCarga de Operaciones...\n" "\x1b[0m");
                // Cargar operaciones en 4 hilos
                for (int i = 3; i < 7; i++) {
                    index[i] = i - 3;
                    pthread_create(&threads[i], NULL, cargar_operaciones, &index[i]);
                }

                for (int i = 3; i < 7; i++) {
                    pthread_join(threads[i], NULL);
                }
                printf("\x1b[32m" "¡Carga de Operaciones Finalizada!\n" "\x1b[0m");
                reporteCargaOperaciones();
                break;
            case 7:
                printf("\x1b[32m" "\nEstados de Cuenta...\n" "\x1b[0m");
                reporteEstadosCuenta();
                break;
            case 8:
                exit(0);
            default:
                printf("Opción no válida\n");
        }
    }

    pthread_mutex_destroy(&mutex);
    return 0;
}