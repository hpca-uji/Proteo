Los archivos de esta carpeta son para ejecutar pruebas con todas las posibles configuraciones.
Se tienen tres ficheros en esta carpeta:
-- run.sh: Para ejecutar una serie de pruebas.
-- arrayRun.sh: Script para ejecutar por slurm para las pruebas. Es llamado por run.sh.
-- singleRun.sh: Para ejecutar pruebas con un fichero de configuración.
-- create_ini.py: Crea un fichero de configuración de tipo "config.ini" a partir de los argumentos pasados

Para ejecutar las pruebas se utiliza el comando:
    bash run.sh grupos-hijos tamaño-matriz cantidad-datos-sincronos tiempo-iteracion proceso-tiempo iteraciones-por-grupo cantidad-nodos
Este script crea subcarpetas en "Results" donde almacena los resultados y los ficheros de configuración que crea. 

grupos-hijos: Es la cantidad de grupos hijos de procesos a ejecutar. Por tanto, el valor 1 indicaría el grupo de procesos padres y un grupo de procesos hijos.
        Actualmente solo funciona con el valor |1|

tamaño-matriz: Cantidad de filas en la matriz rectangular. Esta matriz se utiliza para realizar el computo de la aplicación.

cantidad-datos-sincronos: Indica la cantidad de bytes que se tienen que transmitir desde un grupo de procesos a otro en una redimensión

tiempo-iteracion: Indica la cantidad de segundos que deben pasar para que una iteración se considere terminada.

proceso-tiempo: Ligado al valor "tiempo-iteracion". Indica con cuantos procesos se llega al tiempo por iteracion indicado.
                Si se elige el valor "tiempo-iteracion=2" y "proceso-tiempo=8", con 8 procesos se tardarán 2 segundos por iteración.
                Este valor se utiliza para modificar el tiempo según la cantidad de procesos. Siguiendo el ejemplo, si se pasasen a ejecutar 16 procesos, el tiempo por iteración sería de 1 segundo.
		Si se utiliza el valor 0 para este argumento, todos los procesos tendran un factor=1 (Independiente del numero de procesos, cada iteracion costara "tiempo-iteracion").

iteraciones-por-grupo: Cantidad de iteraciones a realizar en cada grupo para que consideren terminada su ejecución.
                       Actualmente todos los grupos de procesos realizan la misma cantidad de iteraciones.

cantidad-nodos: Cantidad de nodos a utilizar en las ejecuciones. La cantidad de nodos también influye en la cantidad de procesos por grupo, donde nunca habrá más procesos en un grupo 
                que núcleos entre todos los nodos. Si se elige el valor 2 y habiendo 20 núcleos por nodo, no se realizarán pruebas con más de 40 procesos por grupo. 
                
--------------------------------
Para ejecutar una sola prueba con un fichero de configuración se utiliza el siguiente comando:
     sbatch -N valor singleRun.sh configure.ini indiceSalida cantidadPruebas

     - valor: Número de nodos a utilizar.
     - configure.ini: Nombre del fichero de configuración.
     - indiceSalida: Indice del fichero de salida. Si no se quiere usar, cualquier valor entero es válido.
     - cantidadPruebas: Entero que idnca cuantas veces se ejecutarán las pruebas.

Este comando solicita dos argumentos. El primero (valor) es para indicar la cantidad de nodos a usar.
El segundo es el nombre del archivo de configuración a utilizar.
En la carpeta Codes/ se tiene un archivo de configuración de ejemplo llamado "test.ini".
