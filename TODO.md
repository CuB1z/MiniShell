- [X] Ejecutar en foreground líneas con un solo mandato y 0 o más argumentos. (0,5 puntos)
- [X] Ejecutar en foreground líneas con un solo mandato y 0 o más argumentos, redirección de entrada desde archivo y redirección de salida a archivo. (1 punto)
- [X] Ejecutar en foreground líneas con dos mandatos con sus respectivos argumentos, enlazados con ‘|’, y posible redirección de entrada desde archivo y redirección de salida a archivo. (1 punto)
- [X] Ejecutar en foreground líneas con más de dos mandatos con sus respectivos argumentos, enlazados con ‘|’, redirección de entrada desde archivo y redirección de salida a archivo. (2,5 puntos)
- [X] Ejecutar el mandato `cd` (0,5 puntos). Este mandato debe permitir:
    - [X] Acceso a través de rutas absolutas y relativas.
    - [X] Acceso al directorio especificado en la variable HOME si no recibe ningún argumento.
    - [X] Escribir la ruta absoluta del nuevo directorio actual de trabajo. --> Preguntar
    - [X] Ejecutarse sin pipes.
- [ ] Ejecutar tanto en foreground como en background líneas con más de dos mandatos con sus respectivos argumentos, enlazados con ‘|’, redirección de entrada desde archivo y redirección de salida a archivo. Para su correcta demostración, se deben realizar los mandatos internos `jobs` y `bg` (2 puntos):
    - `jobs`: Muestra la lista de trabajos que se están ejecutando en segundo plano o que han sido parados con la señal SIGTSTP (CTRL+Z). El formato de salida será similar al del mandato `jobs` del sistema:
        - `[1]+  Running                 find / -name hola | grep h &`
        - `[2]-  Stopped                 sleep 140`
    - `bg`: Reanuda la ejecución de un job que se paró con CTRL+Z (SIGTSTP). Si se le pasa un identificador, dicho job se reanuda en background. Si no se le pasa ningún identificador, se pasa a background el último trabajo que se paró. Si no existe ningún trabajo parado, no se reanuda nada.
- [ ] Evitar que los comandos lanzados en background y el minishell mueran al enviar la señal SIGINT desde el teclado, mientras los procesos en foreground respondan ante ella. La minishell deberá mostrar una nueva línea y un nuevo prompt cuando reciba dicha señal. Evitar que la minishell se pare cuando se envíe la señal SIGTSTP desde el teclado: debe capturarla y procesarla para parar los procesos que estén ejecutándose en ese momento en foreground, si hubiera alguno. (1 punto)
- [X] Ejecutar el mandato `exit` (0,5 puntos). Este mandato interno termina la minishell de manera ordenada y muestra el prompt de la Shell desde dónde se ejecutó.
- [X] Ejecutar el mandato `umask` (1 punto). Este mandato se utiliza para establecer los permisos por defecto para ficheros nuevos. Debe aceptar un número octal (por ejemplo, 0174). Si se ejecuta sin argumentos debe mostrar la máscara actual y no debe poder ejecutarse con pipes.

**Nota:** Las puntuaciones para cada objetivo parcial son las máximas que se pueden obtener si se cumplen esos objetivos.

**Nota:** No se debe hacer un programa separado para cada objetivo, sino un único programa genérico que cumpla con todos los objetivos simultáneamente.
