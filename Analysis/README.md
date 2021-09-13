Esta carpeta contiene códigos para poder analizar los resultados obtenidos.
Para utilizar los códigos es necesario Python con los módulos Numpy y Pandas.
El código analyser.ipynb necesita además de la aplicación JupyterLab.

Los códigos son los siguientes:

 - Malltimes.py: Recoge los tiempos globales de maleabilidad y ejecución de todos los ficheros pasados como argumento y
     los almacena en dos ficheros CSV para ser utilizados en analyser.ipynb
 - Itertimes.py: Recoge los tiempos locales de iteraciones de un grupo de procesos de todos los ficheros pasados como
     argumento y los almacena en un fichero CSV para ser utilizado en analyser.ipynb
 + Ejemplo de uso de ambos códigos (Esperan los mismos argumentos):
   python3 Malltimes.py NombreFicheros DirectorioFicheros/ NombreCSV
     NombreFicheros: La parte común de los ficheros, los códigos buscan solo aquellos nombres que empiecen por esta cadena.
                     Por defecto, con poner "R" es suficiente.
     DirectorioFicheros/: Nombre del directorio donde se encuentran todos los resultados. Esta pensado para que busque
                     en todos las subdirectorios que tenga en el primer nivel, pero no en segundos niveles o más.
     NombreCSV: Nombre del fichero CSV en el que escribir la recopilación de resultados.

 - analyser.ipynb: Código para ser ejecutado por JupyterNotebook. Dentro del mismo hay que indicar los nombres de los
     ficheros CSV a analizar y tras ellos ejecutar las celdas. Como resultado se obtienen tres ficheros XLSX e imagenes
     en el directorio "Images", y además varios resultados sobre T-test entre varios resultados que se reflejan como
     output en la salida estandar de JupyterNotebook.
