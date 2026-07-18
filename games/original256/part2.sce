; 8-6-89
;
;
;                                           'In this life, all is possible!'
;                                                           Manuel González
;
;
; 'Everything is possible in this life ( but it all costs time and money! )'
;                                                              Tim Gilberts
;
;                                      'And also WE ALL have to work for it'
;                                                            Andrés Samudio
;
;
;
;
;               ╔═════════════════════════════════╗
;               ║ La Aventura Original   2ª Parte ║
;               ╠═════════════════════════════════╣
;               ║ Directed by:  Andrés R. Samudio ║
;               ╟─────────────────────────────────╢
;               ║ Programming by: Manuel González ║
;               ╟─────────────────────────────────╢
;               ║ Graphics by:     Carlos Marqués ║
;               ╚═════════════════════════════════╝
;
;
#ECHO ORIGINAL - PARTE SEGUNDA
;#IF C64
; #ECHO ¡Odio al Commodore!
;#ENDIF
;
;
/CTL    ;Control Section
_       ;Null word character is an underline
/TOK    ;Tokens for Original Adventure part2
*****
_una_
_____
_del_
stás_
_de_l
_que_
n_el_
_con_
abita
_por_
n_la_
ción_
suelo
ente_
_pasa
s_el_
_oso_
e_un_
aguje
iebla
enorm
a_de_
_las_
_está
o_de_
lado_
_esta
_para
_gran
s_un_
pared
entre
ámara
uedes
_hay_
_llev
_tien
File_
o_la_
a_la_
te_ha
tus_p
erna_
a_luz
abism
s_la_
caden
_sale
jaula
puert
adas_
_hach
este_
_incl
inado
UNA_
_se_
_DE_
los_
_al_
_el_
ado_
_un_
_de_
_no_
te_e
osos
_ver
Has_
entr
as_a
Una_
rida
o_a_
_tu_
as_
El_
os_
LA_
ien
_y_
La_
_de
_co
rec
as.
ado
ste
_ca
o_e
ada
tra
a_a
oso
UN_
al_
te_
que
ta.
int
es_
a_
e_
en
ar
er
o_
es
or
an
al
o.
ra
un
ro
ll
in
ca
!_
ci
re
do
ta
._
pa
n_
!!
;       -       -       -       -       -       -       -       -       -
/VOC    ;Vocabulary
S        2 Noun
SUR      2 Noun
E        3 Noun
ESTE     3 Noun
O        4 Noun
OESTE    4 Noun
NE       5 Noun
NORES    5 Noun
NO       6 Noun
NOROE    6 Noun
SE       7 Noun
SURES    7 Noun
SO       8 Noun
SUROE    8 Noun
SUBO     9 Verb
ARRIB    9 Verb
SUBIR    9 Verb
TREPO    9 Verb
TREPA    9 Verb
BAJO    10 Verb
ABAJO   10 Verb
BAJAR   10 Verb
ENTRA   11 Verb
ENTRO   11 Verb
SALIR   12 Verb
SALGO   12 Verb
N       13 Noun
NORTE   13 Noun
I       14 Noun
INVEN   14 Noun
AYUDA   15 Verb
ESPER   16 Verb
DUERM   16 Verb
DORMI   16 Verb
EMPUJ   17 Verb
ABRO    18 Verb
ABRIR   18 Verb
CIERR   19 Verb
CERRA   19 Verb
COJO    20 Verb
COGE    20 Verb
CAZAR   20 Verb
COGER   20 Verb
CAZO    20 Verb
AGARR   20 Verb
ATRAP   20 Verb
DEJO    21 Verb
DEJA    21 Verb
DEJAR   21 Verb
SUELT   21 Verb
SOLTA   21 Verb
DESAT   21 Verb
DESEN   21 Verb
TODO    22 Noun
ENCIE   23 Verb
ENCEN   23 Verb
APAGO   24 Verb
APAGA   24 Verb
SAVE    26 Verb
GRABAR  26 Verb
LOAD    27 Verb
CARGAR  27 Verb
EX      30 Verb
EXAM    30 Verb
EXAMINA 30 Verb
ALUMB   30 Verb
HABLAR  32 Verb
HABLO   32 Verb
DIGO    32 Verb
DECIR   32 Verb
GRITO   32 Verb
GRITAR  32 Verb
PREGU   32 Verb
SALUDO  32 Verb
ECHO    33 Verb
VACIO   33 Verb
VACIA   33 Verb
ECHAR   33 Verb
LLENO   34 Verb
LLENA   34 Verb
Y       35 Conjugation
HUESO   36 Noun
LEO     37 Verb
LEER    37 Verb
LEERLO  37 Verb
COMO    38 Verb
COMER   38 Verb
ASOMO   39 Verb
BEBO    40 Verb
BEBER   40 Verb
BATRA   41 Verb
WHALK   42 Verb
TIMAC   43 Verb
GUáCH   44 Verb
GUACH   44 Verb
ACEIT   45 Verb
CHARC   45 Noun
CANTO   46 Noun
PIEDR   46 Noun
M       47 Verb
D       47 Verb
DESC    47 Verb
DESCR   47 Verb
MIRAR   47 Verb
LINTE   50 Noun
LáMPA   50 Noun
LAMPA   50 Noun
MONED   51 Noun
TORTI   52 Noun
BOTEL   53 Noun
LLAVE   54 Noun
PILA    55 Noun
BATER   55 Noun
PILAS   55 Noun
REJA    56 Noun
JAULA   56 Noun
BARROTE 56 Noun
ALMOH   58 Noun
HACHA   59 Noun
PEPIT   60 Noun
DIAMA   61 Noun
BARRA   62 Noun
TITAN   62 Noun
JOYA    63 Noun
ESPEC   64 Noun
ALFOM   65 Noun
TRIDE   66 Noun
PERLA   67 Noun
HUEVO   68 Noun
CADEN   69 Noun
PIRáM   70 Noun
PIRAM   70 Noun
ESMER   71 Noun
JARRON  72 Noun
JARRóN  72 Noun
GANCH   73 Noun
GARFI   73 Noun
VARIT   74 Noun
XYZZY   75 Noun
PAJAR   76 Noun
PáJAR   76 Noun
NOTA    77 Noun
TEXTO   77 Noun
CUADR   77 Noun
CARTE   77 Noun
APART   78 Verb
SOPLO   79 Verb
SOPLA   79 Verb
NIEBL   80 Noun
USO     81 Verb
USAR    81 Verb
USARLA  81 Verb
AGITAR  81 Verb
AGITO   81 Verb
MOVER   81 Verb
MUEVO   81 Verb
RECOLEC 82 Noun
MAGIA   83 Noun
MAGICA  83 Noun
MáGICA  83 Noun
ALMEJA  85 Noun
OSTRA   85 Noun
DRAGON  86 Noun
DRAGóN  86 Noun
BICHO   86 Noun
ASUSTAR 87 Verb
ATACAR  87 Verb
ATACO   87 Verb
LUCHAR  87 Verb
LUCHO   87 Verb
MATO    87 Verb
MATAR   87 Verb
OIR     88 Verb
OIGO    88 Verb
ESCUCH  88 Verb
REGAR   89 Verb
RIEGO   89 Verb
JUDIA   90 Noun
SEMILLA 90 Noun
PLANTA  90 Noun
BROTE   90 Noun
TRONCO  90 Noun
OSO     91 Noun
PEPOS   91 Noun
FEE     92 Verb
FIE     92 Noun
PUERT   93 Noun
AGUA    94 Noun
JODE    96 Verb
CAPULLO 95 Verb
MARICóN 96 Verb
JODER   96 Verb
FOLLAR  96 Verb
;               FOLLEN  95 Verb
;               GILIPOLLAS 95 Verb
MIERDA  95 Verb
IDIOTA  95 Verb
IMBECIL 95 Verb
CULO    96 Verb
PUTA    96 Verb
CABRON  96 Verb
;               CABRóN  95 Verb
;               JOPUT   95 Verb
RAM     99 Noun
METO   100 Verb
METER  100 Verb
PONGO  100 Verb
PONER  100 Verb
INTRO  100 Verb
TIRO   101 Verb
NADO   101 Verb
BAÑO   101 Verb
TIRAR  101 Verb
LANZO  101 Verb
LANZA  101 Verb
NADAR  101 Verb
BAÑAR  101 Verb
SALTAR 102 Verb
SALTO  102 Verb
FIN    103 Verb
RETIR  103 Verb
ABAND  103 Verb
RINDO  103 Verb
PASO   104 Verb
CRUZA  104 Verb
CRUZO  104 Verb
PASAR  104 Verb
PUENT  105 Noun
DAR    106 Verb
DOY    106 Verb
DARLE  106 Verb
SACO   107 Verb
SACAR  107 Verb
COGEL  108 Verb ; coger en chino
COJER  109 Verb ; coger mal escrito
PLUGH  110 Noun ; clave del original inglés para salir al exterior
PLOVER 110 Noun ; esta clave en el original inglés te lleva al gigante
SERPIE 111 Noun
X      112 Verb
SALIDA 112 Verb
VENTAN 113 Noun ; la ventana que se ve desde el mirador
FIGURA 113 Noun ; o la figura que se ve por ella
ESPEJO 120 Noun ; o el espejo, que es lo que realmente está viendo
BUSCO  114 Verb
BUSCAR 114 Verb
FISURA 115 Noun
PALPAR 116 Verb
PALPO  116 Verb
TOCAR  116 Verb
TOCO   116 Verb
PARED  117 Noun
PILAR  117 Noun
MAQUIN 118 Noun         ; máquina recargadora de pilas
MáQUIN 118 Noun
CACA   119 Noun
#IF !CPC
 AD     120 Verb
#ENDIF
;
;
;
;
;
;-      -       -       -       -       -       -       -       -
;
#IF DRAW
 #INCLUDE T8AO2.SCT
#ELSE
 #INCLUDE T16AO2.SCT
#ENDIF
;       -      -       -       -       -       -       -       -       -
/CON
/0 
/1
/2
/3
O      4
SALIR  3
/4
E      3
O      5
/5
E      4
O      7
/6
/7
E      5
O      8
/8 
E      7
BAJO   9
/9              ; desde esta loc con oeste se va a loc.10 y a loc.11
O     11
SUBO   8
BAJO  38
/10             ; desde esta loc con oeste se va a 12 y 11, con este 11 y 9
O     12
E      9
/11     
O     12
E      9
/12
E     11
S     14
N     20
/13
/14
N     12
S     14
E     14
O     14
NE    12
NO    12
SE    12
SO    12
SUBO  15
BAJO  14
/15
N     15
S     15
E     14
O     15
NE    54
NO    14
SE    14
SO    16
SUBO  14
BAJO  14
/16
N     16
S     16
E     16
O     17
NE    14
NO    16
SE    15
SO    15
SUBO  15
BAJO   7
/17
N     16
S     16
E     17
O     16
NE    17
NO    17
SE    17
SO    17
SUBO  15
BAJO  18
/18
N     17
S     17
E     16
O     18
NE    19
NO    16
SE    17
SO    16
SUBO  18
BAJO  18
/19
N     18
S     19
E     16
O     17
NE    18
NO    18
SE    16
SO    18
SUBO  17
BAJO  19
/20
N     21
S     12
E     20
O     20
SUBO  20
BAJO  20
/21
N     21
S     22
E     21
O     21
SUBO  21
BAJO  20
/22
N     21
S     22
E     23
O     22
SUBO  22
BAJO  22
/23
N     23
S     23
E     22
O     24
SUBO  23
BAJO  23
/24
N     24
S     23
E     24
O     24
SUBO  24
BAJO  25
/25
N     25
S     25
E     25
O     25
SUBO  24
BAJO  25
/26
/27
/28
/29
/30
/31
/32
/33
/34
/35
/36
/37
/38
SUBO   9
SO    53
N       000
/39
S     38
N     40
BAJO  60
/40
S     39
O     41
/41
E     40
/42
/43
/44
/45
/46
/47
/48
/49
S     60
BAJO  50
/50
SUBO  49
BAJO  51
/51
SUBO  50
/52
/53
O     54
NE    38
BAJO  66
/54
E     53
N       000
/55
S     54
N     56
BAJO  70
/56
S     55
/57
/58
/59
/60
NO    70
E     68
O     67
S     16
N     49
SUBO  39
/61
/62
/63
/64
SUBO  66
/65
SUBO  67
/66
BAJO  64
E     67
SUBO  53
/67
BAJO  65
E     60
O     66
/68
O     60
/69
/70
NE    72
SE    60
SO    76
SUBO  55
/71
/72
SO    70
E       000
/73
N     77
ENTRA 77
O       000
/74
S     76
NE      000
N       000
/75
E     84
S       000
SO      000
/76
N     74
S     70
/77
S     73
SALIR 73
/78
BAJO  64
ENTRAR  000
/79
/80
/81
/82
/83
/84
BAJO  88
O     75
/85
/86
/87
/88
SUBO  84
ENTRAR  000
/89
E     88
SALIR 88
/90
BAJO  76
/91
/92
/93
/94
/95
/96
/97
/98
/99
/100
/101
/102
/103
/104
/105
/106    ; gráfico subrutina fin del juego
/107    ; gráfico de la entrada cerrada
/108    ; gráfico del pirata
;       -      -       -       -       -       -       -       -       -
/OBJ    ;Object Definitions
;obj  starts  weight  cont-   wear/     noun   adjective
;num    at            ainer   remove
/0      _       1       _       _       LINTE _
/1      CARRIED 1       _       _       MONED _
/2      _       1       _       _       LINTE _
/3      CARRIED 1       _       _       TORTI _
/4      _       1       _       _       BOTEL _
/5      _       1       _       _       BOTEL _
/6      CARRIED 1       _       _       LLAVE _
/7      _       1       _       _       PILA  _
/8      4       1       _       _       REJA  _
/9      5       1       _       _       VARIT _
/10     _       1       _       _       REJA  _
/11     _       1       _       _       BOTEL _
/12     54      1       _       _       HUESO _
/13     68      1       _       _       ALMOH _
/14     _       1       _       _       HACHA _
/15     4       1       _       _       CANTO _
/16     _       1       _       _       PEPIT _
/17     11      1       _       _       DIAMA _
/18     39      1       _       _       BARRA _
/19     _       1       _       _       JOYA  _
/20     64      1       _       _       ESPEC _
/21     _       1       _       _       ALFOM _
/22     90      1       _       _       TRIDE _
/23     _       1       _       _       PERLA _
/24     78      1       _       _       HUEVO _
/25     _       1       _       _       CADEN _
/26     77      1       _       _       PIRAM _
/27     72      1       _       _       ESMER _         ; alcoba
/28     70      1       _       _       JARRO _
/29     19      1       _       _       GANCH _
;       -      -       -       -       -       -       -       -
/PRO 0                          ;Response table
_     _     ZERO    100
            PROCESS   3
            DONE
#IF !CPC
 AD    _     MESSAGE  162
             DONE
#ENDIF

SALIDAS _   PROCESS  10         ; rutina de salidas
            DONE

_     FIGURA  AT       41         ; donde el espejo
            MESSAGE 138         ; 'la figura te imita.'
            done                ; evitamo que siga con la cognia

ABRIR LINTE LET      33 107
            LET      34  55

LANZO JARRO CARRIED  28
            DESTROY  28
            MESSAGE  32
            DONE   

SOLTA PAJAR LET      33  18     ; abrir
            LET      34  56     ; jaula

S     _     AT       75
            LET      33 104     ; cruzar
;           LET      34 105     ; puente

S     _     AT       77         ; negra
            CLEAR     0
            GOTO     73         ; mágica
            DESC   

E     _     AT       72         ; alcoba
            EQ        1   1
            CARRIED  27
            CLEAR     0
            GOTO     73         ; mágica
            DESC   

E     _     AT       72         ; alcoba
            ZERO      1
            CLEAR     0
            GOTO     73
            DESC   

E     _     AT       72         ; alcoba
            MESSAGE  33
            DONE   

E     _     AT        4
            CLEAR     0
            GOTO      3
            DESC   

; historia del puente:

O     _     AT          9       ; nieblas este
            PRESENT     16      ; pepita de oro
            NOTCARR     16      ; en el suelo
            DESTROY     16      ; la niebla la tapa de nuevo
            CLEAR       230
            CLEAR       102     ; y desaparece de la vista

O     _     AT         9        ; nieblas este
            ZERO       103      ; no hay puente
            CLEAR      160      ; loc 10 a la derecha
            GOTO       10       ; fisura
            DESC

O     _     AT         10       ; fisura
            ZERO       160      ; loc.10 en la derecha
            PROCESS    9        ; te caes

E     _     AT         10       ; fisura
            NOTZERO    160      ; loc.10 en la izquierda
            PROCESS    9        ; te caes

E     _     AT         12       ; nieblas oeste
            ZERO       103      ; no hay puente
            SET        160      ; loc. 10 a la izquierda
            GOTO       10       ; fisura
            DESC

; fin historia del puente de cristal

SALIR _     AT          3       ; entrada
            NOTZERO   152       ; abierta
            WINDOW      7
            CLS
            WINDOW      5
#IF DRAW
            PRINTAT   8      0
#ELSE
            PRINTAT   4      0
#ENDIF
            MESSAGE   145       ; 'la peña de fuera se alegra, ya eres un héroe'
            END

SALIR _     AT          3       ; entrada cerrada
            MESSAGE    33       ; 'algo te impide pasar'
            DONE

SALIR _     AT       77         ; negra
            CLEAR     0         ; enciende luz
            GOTO     73         ; mágica
            DESC   

O     _     AT       73
            EQ        1   1
            CARRIED  27
            SET       0
            GOTO     72         ; alcoba
            DESC   

O     _     AT       73
            ZERO      1
            SET       0
            GOTO     72         ; alcoba
            DESC   

O     _     AT       73
            MESSAGE  33
            DONE   

O     _     AT        3
;            NOTCARR   0
            SET       0
            GOTO      4
;            WINDOW    0
;            CLS
            DESC   

;O     _     AT        3
;            ZERO      8
;            SET       0
;            GOTO      4
;            WINDOW    0
;            CLS         ; PARA QUE SE BORRE EL DIBUJO DE LA ENTRADA
;            DESC

NE    _     AT       74
            LET      33 104
            LET      34 105

SO    _     AT       38         ; habitación de los reyes - serpiente
            GT      104   8     ; serpiente se fué
            GOTO     53
            DESC

SO    _     AT       38
            MESSAGE   8         ; 'la serpiente te marca sus colmillos y se va'
            LET     104   9     ; la serpiente te muerde y se va
            DONE

SO    _     AT       75
            LET      33 104
            LET      34 105

SUBO  _     AT          9
            PRESENT     16     ; pepita de oro
            NOTCARR     16     ; en el suelo
            DESTROY     16     ; la niebla la tapa de nuevo
            CLEAR       230
            CLEAR       102     ; y desaparece de la vista
            NOTDONE

BAJO  _     AT          9
            PRESENT     16     ; pepita de oro
            NOTCARR     16     ; en el suelo
            DESTROY     16     ; la niebla la tapa de nuevo
            CLEAR       230
            CLEAR       102     ; y desaparece de la vista
            NOTDONE

SUBO  JUDIA AT       64
            GT      109   1
            GOTO     78
            DESC   

SUBO  JUDIA AT       64
            MESSAGE  30
            CLEAR   109
            DONE   

ENTRAR _    AT       73         ; habitación mágica
            SET       0         ; apaga luz
            GOTO     77         ; habitación negra
            DESC

ENTRA _     AT       78
            EQ      114 255
            GOTO     90
            DESC   

ENTRA _     AT       78
            MESSAGE  33
            DONE   

ENTRA _     AT       88
            NOTZERO 113
            GOTO     89
            DESC   

ENTRA _     AT       88
            MESSAGE  33
            DONE   

N     _     AT       38
            GT      104   8     ; la serpiente ya no está en el trono
            GOTO     39
            DESC   

N     _     AT       38
            MESSAGE   8         ; 'la serpiente te muerde y se marcha'
            LET     104  9      ; la serpiente te muerde y se va
            DONE   

N     _     AT       54         ; Pantalla del dragón
            EQ      107 255
            GOTO     55
            DESC   

N     _     AT       54         ; el dragón no te deja pasar
            MESSAGE  33         ; '¡algo te impide pasar!'
            DONE

N     _     AT       73         ; habitación mágica
            SET       0         ; apaga luz
            GOTO     77         ; habitación negra
            DESC

N     _     AT       74
            LET      33 104
            LET      34 105

I     _     PROCESS   6
;            CHARSET   1

AYUDA _     PROCESS   5
            DONE   

ESPER _     MESSAGE 101
            DONE   

EMPUJ _     AT        3
            MESSAGE  33
            DONE   

DAR  LINTE  PRESENT   0
            PROCESS  20
            ABSENT    0
            ANYKEY
            DESC

DAR  LINTE  PRESENT   0
            DONE

DAR  _      PROCESS  20
            DONE

ABRO JAULA  NOTAT    38         ; abro jaula del pájaro
            CARRIED  10
            SWAP     10   8
            MESSAGE  11
            CLEAR   122         ; pájaro vuelve a su habitación
            DONE   

ABRO JAULA  LT      104   9     ; abro jaula del pájaro - serp. no había mordido
            CARRIED  10
            SWAP     10   8
            MESSAGE  11
            CLEAR   122         ; pájaro vuelve a su habitación
            MESSAGE  10         ; 'la serpiente huye por el pájarillo'
            SET     104         ; la serpiente desaparece
            CREATE   19
            DONE   

ABRO JAULA  CARRIED  10
            MESSAGE   9         ; 'el veneno te ha matado'
            ANYKEY 
            CLEAR    0
            GOTO     92
            DESC   

ABRO JAULA  AT       88
            CARRIED   6
            MESSAGE  57
            SET     113
            DONE   

ABRO  REJA  AT       88
            MESSAGE  56
            DONE   

ABRO  REJA  CARRIED   8
            MESSAGE 105
            DONE   

ABRO  ALMEJ AT       49
            CARRIED  22
            ZERO    106
            MESSAGE  15
            PLACE    23  51
            SET     106
            DONE   

ABRO  ALMEJ AT       49
            CARRIED  22
            MESSAGE  16
            DONE   

ABRO  ALMEJ AT       49
            MESSAGE  17
            DONE   

ABRO  PUERT AT       78
            EQ      114   1
            SET     114
            MESSAGE  60
            done

ABRO  _     AT        3
            MESSAGE  33
            DONE   

CIERR REJA  CARRIED  10
            MESSAGE 105
            DONE   

USO   VARIT ATGT      9
            ATLT     12
            CARRIED   9
            PLUS    103   1
            GOTO     11
            EQ      103   2
            CLEAR   103
            GOTO     10

USO   VARIT CARRIED   9
            ATGT      9
            ATLT     12
            DESC   

USO   VARIT CARRIED   9
            PRESENT  10
            PROCESS   7

USO   VARIT CARRIED   9
            AT        7
            ZERO    122
            PROCESS   7

USO   VARIT CARRIED   9
            OK     

USO   VARIT LET 33 20         ; COGER VARITA si no la tienes

SACO PILA   NOUN2    MAQUINA    ; sacar la pila recargada de la máquina
            AT       25         ; localidad máquina recarga-pilas
            LET      33  20     ; convierte frase en coger pila

BUSCO _     AT        9         ; localidad de la pepita - nieblas este
            ZERO    102         ; hay niebla
            MESSAGE 143         ; 'la niebla te lo impide'
            DONE

BUSCO PEPIT AT        9         ; localidad de la pepita - nieblas este
            ISAT      16   9
            SYSMESS  49         ; 'está en el suelo!'
            DONE

;BUSCO _     NOTDONE

COJO  _     AT       38
            LT      104   9     ; no te había mordido
            LET     104   9     ; te muerde y se marcha
            MESSAGE   8
            DONE   

COJO  TODO  DOALL   255

COJO  ALMOH ISAT     13 255     ; almohada en el suelo
            ISAT     28 255     ; jarrón chino en el suelo
            NOTAT    68         ; no en la habitación blanda
            GET      13         ; coger almohada
            LET      51  28
            MESSAGE  32         ; 'se ha roto en el suelo'
            DESTROY  28         ; jarrón
            DONE   

COJO  PAJAR AT        7
            NOTCARR   9
            CARRIED   8
            ZERO    122
            SWAP      8  10
            SET     122
            MESSAGE  94
            DONE   

COJO  PAJAR AT        7
            MESSAGE  11
            DONE   

COJO  JUDIA AT       64
            MESSAGE  29
            DONE   

COJO PEPITA AT        9         ; localidad de la pepita - nieblas este
            ZERO    102         ; hay niebla
            MESSAGE 143         ; 'la niebla te lo impide'
            DONE

COJO HACHA  ABSENT   14         ; Actual hacha no está
            AT       78         ; y en la habitacion del gigante
            NOTDONE             ; no puedes

COJO  _     AUTOG  
;            NEWLINE
;            BEEP      1 156
;            BEEP      1 144
;            BEEP      1 168
            DONE
   
COJER _     MESSAGE 114      ; 'Coger es con G'
            DONE

COGEL _     AT       70      ; habitación oliental
            MESSAGE 113      ; 'no sel puñetelo'
            DONE

DEJO  TODO  DOALL   254

;DEJO  LINTE NOTAT    73
;            ATLT     90
;            ATGT      3
;            SET       0

DEJO  JARRO ISNOTAT  13 255     ; almohada no en el suelo
            CARRIED  28         ; jarrón
            NOTAT    68         ; blanda
            WHATO
            MESSAGE  32         ; 'el jarrón se ha roto'
            DESTROY  28         ; jarrón
            DONE   

DEJO  OSO   AT       89         ; SOLTAR OSO
            EQ      111   2
            CARRIED   6         ; tienes la llave
            SET     111
            MESSAGE  43
#IF ST
            PROCESS  14         ; Cambio el dibujo
#ENDIF
#IF AMIGA
            PROCESS  14         ; Cambio el dibujo
#ENDIF
            DONE   

DEJO  OSO   AT       89
            CARRIED   6         ; tienes la llave
            ZERO    111
            MESSAGE  54
            ANYKEY 
            CLEAR    0
            GOTO     92
            DESC   

DEJO  OSO   EQ      111 255
            ATGT     73
            ATLT     76
            ZERO    110
            MES      40
            MESSAGE  41
            SET     110
            CREATE   25
            LET     111   1
            DONE   

DEJO  OSO   EQ      111 255
            MESSAGE  40
            CREATE   25
            LET     111   1
            DONE   

DEJO  _     AUTOD  
;            NEWLINE
;            BEEP      1 168
;            BEEP      1 144
;            BEEP      1 156
            DONE   

ENCIE LINTE EQ        8   2
            MESSAGE  74         ; 'la linterna va mal'
            NOTDONE

ENCIE LINTE CARRIED   2
            NOTZERO 115
            SWAP      2   0
;            CLEAR     0
            DESC   

APAGO LINTE CARRIED   0
            SWAP      0   2
;            NOTAT    73
;            NOTAT     3
;            WINDOW    0         ; si es una pantalla de paso
;            CLS
;            WINDOW    1
;            SET       0
            DESC   

APAGO LINTE CARRIED   2
            OK     

SAVE  RAM   RAMSAVE
            DESC

SAVE  _     SAVE
            DESC

#IF !DRAW
 LOAD  _    NOTEQ    34  99     ; no es RAMLOAD
            GT       29 127     ; graficos
            LOAD
            LT       29 128
            PLUS     29 128

 LOAD  _    NOTEQ    34  99     ; no es RAMLOAD
            LT       29 128     ; modo texto
            LOAD
            GT       29 127
            MINUS    29 128

#ELSE
 LOAD  _    NOTEQ    34  99     ; no es RAMLOAD
            LOAD
#ENDIF

LOAD  RAM   RAMLOAD 248

LOAD   _    NOTEQ    34  99     ; no es RAMLOAD
            DONE

SAVE  _     WINDOW      7       ; Es posible el jugador está en el recolector
            CLS
            DESC                ; despues de LOAD última verbo es SAVE!!

TOCAR PARED PROCESS   4         ; tabla examinar
            DONE

TOCAR _     MESSAGE   55        ; 'no encuentras nada de especial'
            DONE

EX    _     PROCESS   4
            DONE   

OIR   _     AT       40         ; en OID
            MESSAGE  12         ; 'el eco susurra recolector'
            DONE

ECHO  _     CARRIED   5
            EQ       34  94
            LET      34  53

ECHO  BOTEL CARRIED   5
            NOTAT    64
            SWAP      4   5
            OK     

ECHO  BOTEL CARRIED  11
            NOTAT    64
            NOTAT    78
            SWAP     11   4
            OK     

ECHO  BOTEL AT       64
            NOTCARR   5
            NOTCARR  11
            SYSMESS   8
            DONE   

ECHO  BOTEL AT       64
            LET      33  89
            LET      34  90

ECHO  BOTEL AT       78
            LET      33  45     ; aceitar
            LET      34  93     ; puerta

LLENO BOTEL AT       90
            CARRIED   4
            SWAP      4   5
            OK     

LLENO BOTEL CARRIED   4
            AT       41
            SWAP      4   5
            OK     

LLENO BOTEL CARRIED   4
            AT       56
            SWAP      4   5
            OK     

LLENO BOTEL CARRIED   4
            AT       65
            SWAP      4  11
            OK     

LEER  _     LET 34   77        ; NOTA
            PROCESS   4
            DONE   

COMO  HUESO CARRIED  12
            MESSAGE   3
            ANYKEY 
            CLEAR    0
            GOTO     92
            DESC   

COMO  TORTI CARRIED   3
            DESTROY   3
            OK     

BEBO  _     CARRIED   5
            SWAP      4   5
            OK     

BEBO  _     AT       90
            OK     

BEBO  _     AT       41
            OK     

BEBO  _     AT       56
            OK     

ACEIT PUERT AT       78
            CARRIED  11         ; botella con aceite
            ZERO    114
            SWAP     11   4
            MESSAGE  59
            LET     114   1
            done

ACEIT _     NOTDONE

APART NIEBL AT        9
            MESSAGE   4
            DONE   

SOPLO _     AT        9
            ZERO    102
            SET     102

SOPLO _     ZERO    230
            SET     230
            PLACE    16   9

SOPLO _     OK

MATO  _     AT       38         ; reyes - serpiente
            LT      104   9     ; está ahí la serpiente
            MESSAGE   8         ; 'el reptil te marca los colmillos y se va'
            ANYKEY
            MESSAGE   9         ; 'el veneno te ha matado'
            ANYKEY
            CLEAR    0          ; enciende la luz
            GOTO     92
            DESC
  
MATO  _     AT       54
            LT      107 255
            SET     107
            MESSAGE  23
            PLACE    21  54
            DONE   

MATO  _     EQ      118   2
            MESSAGE  88
            DONE   

MATO  _     ATGT     73         ; en el abismo del Troll
            ATLT     76
            ZERO    110
            MESSAGE  45         ; el troll te da una patada
            PROCESS   9         ; caes y te estampas contra el suelo

REGAR JUDIA AT       64
            CARRIED   5
            PLUS    109   1
            SWAP      4   5
            DESC   

REGAR JUDIA AT       64
            CARRIED  11
            MESSAGE  31
            CLEAR   109
            SWAP     11   4
            DONE   

IDIOTA  _   PLUS     33 1       ; mild          INSULTOS
            ZERO    251
            SET     251
            MESSAGE  14
            DONE

PUTA  _     DESTROY   0         ; strong
            DESTROY   2
            MESSAGE  14
            ANYKEY
            DESC

METO  MONED AT       25         ; localidad máquina recarga-pilas
            NOTZERO 117         ; hay algo dentro de la máquina (pila)
            CARRIED   1         ; tienes la moneda
            DESTROY   1         ; la metes
            CLEAR     8         ; energía infinita para la pila
            MESSAGE  84         ; 'chum! pilas recargadas'
            CREATE    7         ; aparece la pila recargada
            DONE
;            ANYKEY
;            DESC

METO  MONED AT       25         ; localidad máquina recarga-pilas
            CARRIED   1         ; tienes la moneda
            DESTROY   1         ; la metes
            SET     117         ; dentro está la moneda
            OK     

METO  PILA  NOUN2    MAQUINA    ; meter la pila en la máquina
            AT       25         ; localidad máquina recarga-pilas
            NOTZERO 117         ; hay algo dentro de la máquina (moneda)
            CARRIED   7         ; tienes la pila
            CLEAR     8         ; energía infinita para la pila
            PLACE     7   25    ; la pila aparece en la repisa de la máquina
            MESSAGE  84         ; 'chum! pilas recargadas'
            DONE
;            ANYKEY
;            DESC

METO  PILA  NOUN2     MAQUINA   ; meter pila en la máquina
            AT       25         ; localidad máquina recarga-pilas
            CARRIED   7         ; tienes la moneda
            DESTROY   7         ; la metes
            SET     117         ; dentro está la pila
            OK     

METO  PILA  CARRIED   7
            CARRIED   2
            DESTROY   7
            SET     115
            OK
     

METO PILA   NOTCARR   7  ; si no tienes la pila
            DROP      7  ; te dice 'pero si no la tienes'
            DONE

TIRO  BOTEL CARRIED   4
            DESTROY   4
            MESSAGE  32
            DONE   

TIRO  BOTEL CARRIED   5
            DESTROY   5
            MESSAGE  32
            DONE   

TIRO  BOTEL CARRIED  11
            DESTROY  11
            MESSAGE  32
            DONE   

TIRO  HACHA CARRIED  14
            ZERO    119         ; NO HAS FALLADO
            EQ      118   2
            CHANCE   50
            MESSAGE  90
            PLACE    14 255
            SET     119
            DONE   

TIRO  HACHA CARRIED  14
            EQ      118   2
            MESSAGE  91         ; HAS MATADO ENANO
            DESTROY  14
            CLEAR   118
            CLEAR   119
            PLUS    120   1
            DONE   

;TIRO  HACHA CARRIED  14
;            EQ      118   2
;            NOTZERO 119
;            MESSAGE  90
;            ANYKEY
;            PROCESS   8
;            MESSAGE  89                 ; TE HA MATADO
;            GOTO     92
;            ANYKEY
;            DESC


TIRO  _     AT       90
            MESSAGE   3
            CLEAR    0
            GOTO     92
            ANYKEY 
            DESC   

TIRO  _     AT       56
            MESSAGE   3
            CLEAR    0
            GOTO     92
            ANYKEY 
            DESC   

TIRO  _     WHATO  
            EQ       54 254
            PUTO    255
            OK     

TIRO  _     SYSMESS  28
            DONE   

FIN   _     QUIT
            TURNS  
            END
    
FIN   _     DESC

PASO  _     ATGT     73
            ATLT     76
            ZERO    110
            MESSAGE  36
            DONE   

PASO  _     ATGT     73
            ATLT     76
            EQ      111 255
            MESSAGE  44
            CLEAR    0
            GOTO     92
            ANYKEY 
            DESC   

PASO  _     AT       74
            GOTO     75
            DESC   

PASO  _     AT       75
            GOTO     74
            DESC   

PASO  _     AT       11
            GOTO     12
            DESC   

PASO  _     AT       12
            GOTO     11
            DESC   

;           ATGT      3
;            ATLT     90
;            NOTAT    73
SACO  PILA  CARRIED   0
            CLEAR   115
            PLACE     7 254
            SWAP      0   2
;            WINDOW    0         ; si es una pantalla de paso
;            CLS
;            WINDOW    1
;            SET       0
            DESC   

;SACO  PILA  CARRIED   0
;            CLEAR   115
;            PLACE     7 254
;            SWAP      0   2
;            CLEAR     0
;            DESC

SACO  PILA  CARRIED   2
            NOTZERO 115
            PLACE     7 254
            CLEAR   115
            OK     

SALTO _     AT       68         ; habitación blanda
            MESSAGE 112         ; 'boing! boong!'
            DONE

SALTO _     AT       10         ; en la fisura sin puente
            PROCESS   9         ; saltas y te caes

SALTO _     ATGT     73         ; en el abismo
            ATLT     76         ; del Troll (74-75)
            PROCESS   9         ; saltas y te caes

M     _     DESC

FEE  FIE   AT       78         ; habitación del gigante
            ISAT     24  78
            MESSAGE  49         ; 'el huevo vibra'
            DONE   

FEE   FIE   AT       78         ; habitación del gigante
            CARRIED  24
            PLACE    24  78
            MES      46         ; 'se te ha caido'
            MESSAGE  49         ; 'el huevo vibra'
            DONE   

FEE   FIE   AT       78         ; habitación del gigante
            MESSAGE  50
            CREATE   24
            DONE
   
FEE   FIE   MESSAGE 115         ; la magia hace temblar la cueva
            DONE
   
_ RECOL     AT       40
            CLEAR     0
            GOTO     91
            DESC   

;_ RECOL     AT       91
;            NOTZERO   8         ; fucking shithot son !
;            CARRIED   0
;            GOTO     40
;            CLS
;            DESC

_ RECOL     AT       91
            SET       0
            GOTO     40
            CLS
            DESC
   
_ RECOL     MESSAGE 115         ; la magia hace temblar la cueva
            DONE


_ MAGIA     AT       40
            CLEAR     0
            GOTO     73
            DESC   

;_ MAGIA     AT       73
;            SET       0
;            NOTZERO   8
;            CARRIED   0
;            CLEAR     0

_ MAGIA     AT       73
            CARRIED  27         ; llevas la esmeralda
            PLACE    27  73     ; deja la esmeralda en habitación mágica

_ MAGIA     AT       73
            SET       0
            GOTO     40
            DESC
   
_ MAGIA     MESSAGE 115         ; la magia hace temblar la cueva
            DONE

_     XYZZY AT        5
            CLEAR     0
            GOTO     91
            DESC   

;_     XYZZY AT       91         ; recolector
;            SET       0
;            CARRIED   0
;            NOTZERO   8
;            CLEAR     0

_     XYZZY AT       91         ; recolector
            CLS
            SET       0         ; ****
            GOTO      5
            DESC
   
_     XYZZY MESSAGE 115         ; la magia hace temblar la cueva
            DONE


_     PLUGH MESSAGE 115         ; la magia hace temblar la cueva
            DONE

SALUDO _    MESSAGE  14  ; ESTA ENTRADA DEBE ESTAR DESPUES DE LAS PALABRAS CLAVE
            DONE                ; 'nadie te hace caso'
   
_     _     AT       48
            CLEAR   105
            SYSMESS   7
            DONE

_       _   LT      33      14
            MOVE    38
            DESC

_       _   LT      33      14
            SYSMESS 7                   ; 'no puedes '
            EQ      33      13          ; norte
            SYSMESS  3                  ; 'ir al '

_       _   LT      33      9
            SYSMESS  3                  ; 'ir al '

_       _   LT      33      14
            NEWTEXT
            COPYFF  33      200
            PLUS    200     116
            MES     [200]
            SYSMESS 4                   ; '.'
            PROCESS 10

/PRO 1
_       _   AT        0
            WINDOW    5
#IF !DRAW
            WINAT     0    10
            LT        29  128   ; 80 columnas modo texto
            WINAT     0    22
#ELSE
 #IF S48
            WINAT       0       5
 #ELSE
  #IF MSX
            WINAT       0       5
  #ELSE
            WINAT       0       4
  #ENDIF
 #ENDIF
#ENDIF

_     _     AT       0
            SET      115
            LET      8 200
            PROMPT   2
            LET      53 64     ; listado de objetos todo junto
            ABILITY  7 255
            NOTZERO  255       ; no es la primera partida
            COPYFF   254 33    ; recuerda la clave de la primera partida
            PROCESS  3         ; proceso de chequeo de la clave
            DESC

_     _     AT        0
            WINDOW    0
#IF !DRAW
            LT       29   128   ; modo texto
            GRAPHIC   3     0   ; no borra pantalla cuando está en modo texto

 _     _     AT        0
#ENDIF
            SET      255        ; Init solamente la primera vez
            PICTURE   1

_     _     AT        0
            WINDOW    5
            PRINTAT   5   6
            MES       0
            PRINTAT  11   9
            MES     107                 ; programa
            PRINTAT  13   11
            MES      61
            PRINTAT   7   10
            MES     106
            PRINTAT  12   8
            MES       2                 ; manuel gonzález
            PRINTAT  10   8
            MES       6                 ; andrés samudio
            PRINTAT  14   8
            MES      77
            PRINTAT   15   9
#IF !PC
 #IF !DRAW
            SYSMESS  5                  ; vte. misas
 #ENDIF
#ENDIF
            PRINTAT  17   5
            MES      83
            PRINTAT  9   11
            MES      140                 ; dirección
            GOTO     1
            INPUT    6  0
            WINDOW   6
            MODE     2          ; no more ventanita de input de la clave
#IF S48
            WINAT    21 0       ; hay una línea menos en el Spectrum...
#ELSE
 #IF MSX
            WINAT    21 0       ; ...y en el MSX
 #ELSE
            WINAT    22 0
 #ENDIF
#ENDIF
            DONE

#IF ST
 _     _     AT       92               ; muerte para ST y AMIGA
             GT       29 127
             WINDOW   0
             CLS
             PICTURE  200               ; muerte ST
             WINDOW   1

 _     _     AT       89               ; El oso
             PROCESS  14
#ENDIF
#IF AMIGA
 _     _     AT       92               ; muerte para ST y AMIGA
             GT       29 127
             WINDOW   0
             CLS
             PICTURE  200               ; muerte ST
             WINDOW   1

 _     _     AT       89               ; El oso
             PROCESS  14
#ENDIF
_     _     AT       92
            TURNS  
            END    

_     _     AT       91         ; En el recolector
            PROCESS  15         ; lista objetos
;           NEWLINE
            LT      150  4      ; quedan menos de 4 tesoros por poner
            GT      150  1      ; pero no es el último
            MES     136         ; 'Te faltan '
            PRINT   150         ; nº de tesoros que faltan por poner
            MESSAGE 131         ; punto final

_     _     AT       91         ; En el recolector
            EQ      150  1      ; es el último tesoro
            MESSAGE 137         ; 'Sólo queda el último.'

_     _     AT        3         ; en la entrada a la caverna
            ZERO    152         ; entrada cerrada
            NOTZERO 151         ; cataclismo se acerca
            WINDOW    0
            PICTURE 107         ; gráfico cueva cerrada
            WINDOW    1
;            NEWLINE            ; por qué capullo?
            MESSAGE 146         ; 'la puerta se abre misteriosamente...'
            SET     152         ; entrada abierta
            ANYKEY
            DESC

_     _     PROCESS  18         ; mensajes de oscuro!

_     _     AT       89         ;DAR
            CLEAR   110

_     _     AT        3         ; en la entrada
            ZERO    152         ; entrada cerrada
            WINDOW    0
            PICTURE 107         ; entrada con reja
            WINDOW    1
            MES     147         ; descripción localidad

_     _     AT        3         ; en la entrada
            NOTZERO 152         ; entrada abierta
            WINDOW    0
            PICTURE 106         ; entrada sin reja
            WINDOW    1
            MES     148         ; 'salida al exterior'

_     _     ATGT      2         ; no en la entrada ni presentación
            NEWLINE
            NOTAT    91         ; no estás en el recolector
            ZERO      0         ; hay luz
            LISTOBJ
#IF DRAW
            ZERO     40         ; no se ha dibujado gráfico
            WINDOW    0
            PICTURE  95         ; pantalla de paso
            WINDOW    1
#ENDIF

_     _     NOTAT    91         ; no en el recolector
            ATGT      2         ; no en la entrada ni presentación
            NOTZERO   0         ; no hay luz
            PRESENT   0         ; pero hay linterna encendida
            LISTOBJ
#IF DRAW
            ZERO     40         ; no se ha dibujado gráfico
            WINDOW    0
            PICTURE  95         ; (porque no hay gráfico o porque hay oscuridad)
            WINDOW    1
#ENDIF

_     _    NOTAT    91         ; Necesario para ST si hay un dragon
           ATGT     2
           NOTZERO  0           ; no hay luz
           ABSENT   0           ; no hay linterna encendida
#IF !DRAW                       ; Si hay un modo texto
            GT       29 127
#ENDIF
           WINDOW   0
           CLS
           WINDOW   1

/PRO 2
_     _     AT        2
            WINDOW    7
            CLS
            SET       100
            TIME      30   1    ; ni cuando 'pulsa una tecla' ni 'más'
            INPUT      0   4    ; REPITE EN TIMEOUT
            WINDOW     1
#IF !DRAW
            GT         29 127
#ENDIF
#IF  ST
            WINAT      13 0     ; Hay un más linea en ST y AMIGA
#ELSE
 #IF AMIGA
            WINAT      13 0
 #ELSE
            WINAT      14 0
 #ENDIF
#ENDIF

#IF !DRAW
 _       _   AT          2
             LT          29      128     ; modo texto
             GRAPHIC     3       0       ; sin gráficos y sin cls, sin anykey
#ENDIF

_     _     AT          2
            WINDOW      0
            CLS
            PICTURE   106       ; cueva abierta
            WINDOW      1
            MESSAGE   150       ; 'la puerta se cierra mágicamente...'
            ANYKEY
            GOTO        3       ; Empezar juego
            DESC

_     _     AT    1             ; CLAVE
            PAUSE     100
            CLS
            MES      1
            DONE

;_     _     EQ        8   1
;            LET       8   2
;            NOTAT    73
;            ATLT     90
;            ATGT      3
;            SWAP      0   2
;            WINDOW    0         ; si es una pantalla de paso
;            CLS
;            WINDOW    1
;            SET       0
;            DESC
;
;_     _     TIMEOUT
;            ZERO    101
;           SET     101
;           PROCESS  14

_     _     NOTZERO   0         ; Cuando oscuro sin linterna
            ABSENT    0
            ZERO      9         ; no ha contado
            SET       9         ; empezar

_     _     PRESENT   0         ; Cuando la linterna esta
;            ZERO      8
;            SET       9
            CLEAR     9         ; ya esta bien

_       _   ZERO      0         ; o hay luz
            CLEAR     9         ; tambien

_     _     ISAT      0 252     ; si la linterna esta apagada?
            NOTZERO   8         ; no hay maximo
            PLUS      8   1

_     _     EQ        8   1
            LET       8   2
            PRESENT   0
            SWAP      2   0     ; It's a gonna mate
            DESC

_     _     LT      118   2
            SET       7

_     _     EQ        7 250
            PROCESS   8         ; ENANO TE MATA
            MESSAGE  89         ; TE HA MATADO
            PROCESS  17         ; dibuja loc actual
            CLEAR    0
            GOTO     92
            ANYKEY 
            DESC   

_     _     PLUS    104   1     ; serpiente
            NOTAT    38
            LT      104   9     ; si no te había mordido
            CLEAR   104         ; te da 7 oportunidades más

_     _     EQ      104   9     ; te acaba de morder
            MESSAGE   8         ; 'la serp te muerde'

_     _     EQ      104  16     ; veneno de la serpiente te mata
            MESSAGE   9         ; 'el veneno te ha matado'
            CLEAR    0
            GOTO     92
            ANYKEY 
            DESC   

_     _     PRESENT   0         ;LINTE
            EQ        8  60
            MESSAGE  74         ; 'la linterna va mal'
;            BEEP    100  50
;            ANYKEY
            MINUS     8   1
;            DESC

_     _     PRESENT   0         ;LINTE
            NOTZERO   8
            LT        8  50
            MESSAGE  75         ; 'la linterna apenas da luz'
;            EQ        8   1
;            LET       8   2

_     _     LT        9 250     ;LINTE
            NOTZERO   9
;            CLEAR     0
;            CLS
            MESSAGE  76
;            BEEP    100  50
            ANYKEY 
            CLEAR    0
            GOTO     92
            DESC   

_     _     AT       54         ;DRAGO
            ZERO    107
            LET     107   4

_     _     AT       54         ;DRAGO
            LT      107 255
            GT      107   1
            MINUS   107   1

_     _     PROCESS  19         ; sólo con luz

_      _    LT      118   2     ;(MATO)  el enano aparece y lanza hacha
            LT      120   5
            CHANCE    9
            ATGT     43
            ATLT     68
            NOTAT    47
            NOTAT    52
            NOTAT    53
            NOTAT    54
            NOTAT    55
            NOTAT    56
            NOTAT    58
            NOTAT    59
            NOTAT    91
            MESSAGE  85
            PLUS    118   1
            ANYKEY 
            MESSAGE  22
            ANYKEY              ; EN LUGAR DEL       'BEEP  250 60'
            NEWTEXT
            EQ      118   1
            PROCESS   8         ; el enano te arroja el hacha y...
;            WINDOW  0
;            PICTURE  [38]      ; LOC ACTUAL
            PROCESS  17         ; El dibujo para aqui
;            WINDOW  1
            MESSAGE  109
            MESSAGE  87
            PLACE    14 255
            ANYKEY
            DESC

;_     _     ATGT     37         ; pirata puede que aparezca
;            ATLT     40
;            GT      104   8     ; la serpiente se fué
;            PROCESS  16         ; pirata te quita un objeto (sólo el 20%)

;_     _     ATGT     52         ; pirata puede que aparezca
;            ATLT     57
;            PROCESS  16         ; pirata te quita un objeto (sólo el 20%)

_       _   NOTAT      3        ; El jugador no esta en la primer loc.
            NOTAT     38        ; o La serpiente
            NOTAT     91        ; o El recolector
            NOTAT     89        ; o El oso
            NOTAT     54        ; o El dragon.....
            NOTAT     19        ; o la localidad del pirata
            PROCESS   16

_     _     AT       91         ; recolector, fin de recolección
            ZERO    151         ; todavía no ha salido Elfito
            CLEAR   121
            DOALL    91         ; loc. recolector
            WHATO  
            GT       51  15
            PLUS    121   1
            EQ      121  14
            PAUSE   100
            CLS
            WINDOW    5         ; ventana en centro de pantalla
            PRINTAT  10  0
            MESSAGE  48
            ANYKEY
            CLS
#IF DRAW
            PRINTAT   8   0
#ELSE
            PRINTAT   6   0
#ENDIF
            MESSAGE  93         ; 'felicidades, y elfito dice: "corre!".'
            ANYKEY
            CLS
            LET     151  11     ; 11 jugadas para el cataclismo
            DESC

_     _     NOTZERO 151         ; falta poco para el cataclismo
            MINUS   151   1     ; falta una jugada menos
            ZERO    151         ; llega el cataclismo
            CLS
            MESSAGE  51         ; 'el cataclismo te mata'
            ANYKEY
            CLS
            CLEAR    0
            GOTO     92
            DESC

/PRO 3
_     _     COPYFF    33 254    ; recuerda la última clave

BATRA _     PLACE     2 254
            PLACE     5 254
            GOTO      2
            DONE

WHALK _     PLACE     2 254
            PLACE     4 254
            GOTO      2
            DONE

TIMAC _     PLACE     0 254
            PLACE     5 254
            GOTO      2
            DONE

GUACH _     PLACE     0 254
            PLACE     4 254
            GOTO      2
            DONE

_     _     CLEAR     254
#IF !CPC
 JODE  _     MES       14
             DONE
#ENDIF

FIN   _     CLS
            CLEAR     255
            END

AYUDA _     MES   105
            DONE

NO    _     MES    25        ; 'piensa un poco, es fácil'
            DONE

_     _     MESSAGE   64

/PRO 4
; tabla de examinar

PALPAR PARED AT       40        ; OID
            MESSAGE 100         ; 'las letras están grabadas en la piedra'
            DONE                ; esta entrada funcionará incluso a oscuras

_     _     NOTZERO   0
            ABSENT    0         ; sin linterna con luz
            MESSAGE  99         ; 'no se ve bien'
            DONE   


_    SEMIL  AT       64
            MESSAGE  24
            DONE


_    ESPEJO AT       55
            MESSAGE   139
            DONE

_    ALMEJA AT       49
            MESSAGE  157        ; algo sobre el tridente
            DONE

_    LLAVE  CARRIED 6
            MESSAGE 158         ; del zoo
            DONE

_    CACA   AT      54
            MESSAGE 159         ; cochino
            DONE

_    PUENTE ATGT     73         ; puente del troll
            ATLT     76
            MESSAGE 153         ; 'es un puente colgante'
            DONE

_    PUENTE AT       11         ; puente cristal
            MESSAGE 154         ; 'es un puente mágico'
            DONE

_     HACHA AT       78         ; habitación del gigante
            MESSAGE 155         ; 'es muy grande'
            DONE

_     TORTI PRESENT   3         ; la tortilla
            MESSAGE 156         ; 'se la comería hasta un oso'
            DONE

_     LINTE NOTZERO 115
            LET      34  55

_     PILA  NOTZERO 115
            ABSENT    0
            ABSENT    2
            SYSMESS  28
            DONE   

_     PILA  ZERO    115
            ABSENT    7
            SYSMESS  28
            DONE   

_     PILA  MES      98
            NOTZERO   8
            PRINT     8
            NEWLINE
            DONE   

_     PILA  MESSAGE 102
            DONE   

_     REJA  AT        3
            MESSAGE  17         ; 'es demasiado fuerte'
            DONE

_     REJA  AT       88         ; jaula del oso
            ZERO    113
            MESSAGE  17
            DONE   

_     REJA  PRESENT    8        ; jaula vacía
            MESSAGE  141        ; 'la jaula está vacía'
            DONE

_     REJA  CARRIED   10        ; jaula con pájaro
            MESSAGE   94        ; 'tienes al pájaro enjaulado'
            DONE

_    PAJARO CARRIED   10        ; jaula con pájaro
            MESSAGE   94        ; 'tienes al pájaro enjaulado'
            DONE

_    PAJARO AT         7        ; cámara espléndida
            MESSAGE  142        ; 'es un lindo pajarito'
            DONE

_     NOTA  AT        9         ; nieblas
            MESSAGE  34         ; 'aquí se perdió la pepita de oro'
            DONE   

_     NOTA  AT        5         ; habitación XYZZY
            MESSAGE  92         ; 'pone XYZZY'
            DONE   

_     NOTA  AT       40         ; OID
            MESSAGE  108        ; 'pone OID'
            DONE   

_     NOTA  AT       77         ; habitación negra
            MESSAGE 149         ; 'gracias por traerme luz aquí'
            DONE

_ SERPIENTE AT       38
            LT       104  9     ; no te ha mordido
            MESSAGE  116
            DONE

_   FISURA  ATGT      9         ; en la fisura sin puente
            ATLT     12         ; o con puente
            MESSAGE 144         ; 'la fisura es una raja'
            DONE

_       _   WHATO
;           SAME     54  38     ; el objeto está contigo
;           GT       51  15     ; es un tesoro
;           MESSAGE  63         ; 'tesoro: _'
;           DONE
;
;       _   SAME     54  38     ; el objeto está contigo
;           LT       51  16     ; es un objeto normal
;           SYSMESS  10         ; 'objeto: _'
;           NEWLINE
;           DONE
;
_       _   PRESENT [51]        ; el objeto está presente
            GT       51  15     ; es un tesoro
            MESSAGE 151         ; '@ tesoro'
            DONE

_       _   PRESENT  [51]       ; el objeto está presente
            LT       51  16     ; es un objeto normal
            MESSAGE 152         ; '@ objeto'
            DONE

_     PARED AT        5
            MESSAGE 100         ; 'las letras están grabadas en la piedra'
            DONE   

_     PARED AT       40
            MESSAGE 100         ; 'las letras están grabadas en la piedra'
            DONE   

_     _     MESSAGE  55         ; 'no le encuentras nada especial'
            DONE   


/PRO 5

_     _     ATGT      13        ; en los laberintos
            ATLT      26        ; de la pila y del pirata
            MESSAGE  103        ; 'hazte un mapa'
            DONE

_     _     AT       73         ; habitación mágica
            MESSAGE  110        ; utiliza la magia
            DONE

_     _     AT       5          ; desechos
            MESSAGE  110        ; utiliza la magia
            DONE

_     _     AT       40         ; desechos
            MESSAGE 161        ; usa otros sentidos o magia
            DONE

_     _     AT       10         ; fisura sin puente
            MESSAGE  110        ; utiliza la magia
            DONE

_     _     NOTCARR   0
            MESSAGE  35
            DONE   

_     _     AT        9
            MESSAGE   5
            DONE
   
_     _     MESSAGE  25
            DONE   


/PRO 6

I     _     NOTZERO   0
;            NOTZERO   8
            ABSENT    0
            MESSAGE  99
            DONE   

I     _     NOTZERO     1       ; TIENES ALGUN OBJETO
            SYSMESS     9       ; 'TIENES:'
            LISTAT      CARRIED
;            NEWLINE
            DONE

I     _     SYSMESS     11        ; 'NO TIENES NADA'
;            NEWLINE

/PRO 7          ;  pajarillo se convierte en dragón

#IF DRAW
 _     _     WINDOW      7
#ELSE
 _     _     GT          29     127    ; no modo texto
             WINDOW      7
#ENDIF
            CLS                 ; cls para borrar toda la pantalla
            WINDOW      2
            WINSIZE    18      30      ; el dragón ocupa 144 líneas
            PICTURE     82      ; porque el dragón no usa toda la zona de dibujo
            ANYKEY
            WINDOW      6
            MODE        2
#IF !DRAW
            WINAT       15      25
#ELSE
 #IF S48
            WINAT       15      16
 #ELSE
  #IF MSX
            WINAT       15      16
  #ELSE
            WINAT       15      14
  #ENDIF
 #ENDIF
#ENDIF
            WINSIZE  6  16
            CLS    
            MES      96
            ANYKEY 
            WINDOW   7
            CLS
            GOTO     92
            CLEAR    0
            DESC

#IF !DRAW
 _     _     MESSAGE  96
             ANYKEY
             GOTO     92
             CLEAR    0
             DESC
#ENDIF

/PRO 8    ; el enano te lanza hacha y...

#IF !DRAW
 _    _     LT  29 128     ; MODO TEXTO
            MESSAGE  86
            ANYKEY
            DONE
#ENDIF

_     _     WINDOW    7                 
            CLS
            SET       7
            WINDOW    2
            PICTURE  43         ; enano
            WINDOW    6
            WINAT    21 5
            WINSIZE   4 20
            MES      86
            ANYKEY 
            WINDOW    7
            CLS    
            WINDOW    1
            CLS

/PRO 9  ; saltas y te caes al fondo del abismo o de la fisura

_     _     MESSAGE  111        ; 'caes y te estampas contra el suelo'
            ANYKEY
            GOTO     92         ; muerto
            CLEAR    0
            DESC

/PRO 10                         ; Rutina de salidas
_       _       CLEAR   201             ; cuantos mensajes
                LET     200     117     ; no salida
                PROCESS 13              ; empieza con Norte
                PROCESS 11
                GT      201     1
                MES     133             ; ' y '

_       _       EQ      201     1
                MES     134             ; 'La única salida es'

_       _       MES     [200]           ; salida final (o ninguna)
                MESSAGE 131             ; '.'

/PRO 11                         ; para cada movimiento
_       _       COPYFF  38      202     ; copia loc actual
                MOVE    202             ; intenta moverse
                PROCESS 12              ; cuando lo consigue

_       _       PLUS    33      1       ; próxima dirección
                LT      33      13      ; no se ha pasado aún
                REDO                    ; repite el proceso

/PRO 12                         ; Imprime la salida
_       _       EQ      201     1
                MES     130             ; 'Salidas posibles son'

_       _       GT      201     1       ; no la primera
                MES     132             ; ',_'

_       _       NOTZERO 201
                MES     [200]           ; último mensaje en buffer

_       _       COPYFF  33      200     ; calcula la dirección
                PLUS    200     116     ; del próximo mensaje
                PLUS    201     1       ; prepara el siguiente

/PRO 13                         ; Proceso para imprimir NORTE en 1er lugar
_       _       LET     33      13      ; comienza con Norte
                COPYFF  38      202
                MOVE    202             ; mira a ver si se puede ir al Norte
                PROCESS 12              ; Si se puede lo imprime

_       _       LET     33      2       ; sigue con Sur

/PRO 14         ; El oso
#IF ST
 _     _     NOTZERO    0
             ABSENT     0
             DONE

 _     _     ZERO     111              ; is chained
             WINDOW    0
             PICTURE 202               ; dibujo del oso ST
             WINDOW    1
             DONE

 _     _     EQ       111 2            ; o contento
             WINDOW    0
             PICTURE 202               ; dibujo del oso ST
             WINDOW    1
             DONE

 _      _    WINDOW    0
             PICTURE  95               ; pantalla de paso
             WINDOW    1
#ENDIF
#IF AMIGA
 _     _     NOTZERO    0
             ABSENT     0
             DONE

 _     _     ZERO     111              ; is chained
             WINDOW    0
             PICTURE 202               ; dibujo del oso AMIGA
             WINDOW    1
             DONE

  _     _    EQ       111 2            ; o contento
             WINDOW    0
             PICTURE 202               ; dibujo del oso AMIGA
             WINDOW    1
             DONE

 _      _    WINDOW    0
             PICTURE  95               ; pantalla de paso
             WINDOW    1
#ENDIF
/PRO 15
_     _     WINDOW    7
            CLS
            WINDOW    4                 ; Use window 4 in case CPC
            WINSIZE   24     127        ; Max size but not last line
#IF !DRAW
             PRINTAT       0     12
             LT          29     128    ; 80 columnas modo texto
             PRINTAT       0     26
#ELSE
 #IF S48
             PRINTAT       0     7
 #ELSE
  #IF MSX
             PRINTAT       0     7
  #ELSE
             PRINTAT       0     6
  #ENDIF
 #ENDIF
#ENDIF
_     _     MESSAGE 135                 ; 'Estás en el recolector'
            LET     150   14            ; nº de tesoros a poner en recolector
            DOALL   255
            WHATO  
            GT       51   15            ; el objeto es un tesoro
            MESSAGE  63                 ; 'tesoro: _'
            MINUS   150    1            ; contador tesoros que faltan

_     _     LT       51  16
            SYSMESS  10
            NEWLINE

/PRO 16                 ; el pirata te quita un tesoro... (si puede)

_       _   ISNOTAT     29      19      ; El garfio no esta en la loc. de pirata
            DONE

_       _   WHATO
            LT          51      16      ; objeto no es un tesoro
            COPYFF      46      34      ; IT >> Noun
            WHATO
            LT          51      16      ; objeto no es un tesoro
            DONE

_       _   NOTEQ       54      254     ; el jugador no tiene un tesoro
            NOTSAME     54      38      ; o no esta en la actual
            DONE                        ; no es possible por la pirata salir

_       _   CHANCE      80              ; pirata da la coña de vez
            DONE                        ; en cuando; sólo 20% de las veces

_       _   PUTO        19
#IF !DRAW
            GT          29     127  ; modo graficos
            WINDOW      7
            CLS                     ; cls después para borrar toda la pantalla
            WINDOW      2
            PICTURE     108         ; gráfico del pirata
            WINDOW      6
            MODE        0           ; more para el texto del pirata
 ;            WINAT       15      25
 ;            WINSIZE     7       18
            WINAT       21        0
            WINSIZE      3      127
            CLS    
 ;           WINDOW   1
            MES  97                  ; 'aparece pirata y te quita un objeto'
            ANYKEY
            WINDOW    7
            CLS
 ;           WINDOW    0
 ;           PICTURE   [38]               ; loc. actual
 ;           WINDOW    1
            PROCESS  17
            CLS
            MESSAGE  87
            ANYKEY
            DESC
#ENDIF
_       _  MESSAGE  97                  ; 'aparece pirata y te quita un objeto'
           ANYKEY
           MESSAGE  87
/PRO 17                         ; dibuja loc. actual
_     _     NOTZERO   0
            ABSENT    0
            DONE

_       _       WINDOW  0               ; abrir ventana de dibujos
                PICTURE [38]            ; localidad actual
#IF DRAW
                ZERO    40              ; si no hay
                PICTURE 95              ; pantalla de paso
#ENDIF

_       _       WINDOW  1

; abajo de aqui esta todo los mensajes no puedes cuando esta oscuro - creo..
; desde process 1.
/PRO 18
_     _     NOTZERO   0
            ABSENT    0
            DONE

_     _     AT       54         ; localidad del dragón
            LT      107 255
#IF !DRAW
            GT       29 127     ; modo gráfico
#ENDIF
            WINDOW    7
            CLS    
            WINDOW    2
            WINSIZE  18 30
            PICTURE  82         ; dragón
            ANYKEY 
            WINDOW    7         ; window 0 = papel verde en sp
            CLS

_     _     AT       54
#IF !DRAW
            GT       29 127     ; modo gráfico
#ENDIF
            WINDOW    0
            PICTURE   95        ; pantalla de paso
            WINDOW    1         

_     _     AT       54         ;DRAGO
            MES      18
            LT      107 255
            MES      19

_     _     ATGT     73
            ATLT     76
            ZERO    110
            MES      36

_     _     AT       38         ; serpiente
            LT      104   9     ; está aquí
            MES       7

_     _     AT        7         ; localidad del pájaro
;            PRESENT     0       ; con la linterna encendida
            ZERO    122
            MES      95


_     _     AT       78         ;PUERT
            ZERO    114
            MES      58

_     _     AT       78         ;PUERT
            EQ      114   1
            MES      59

_     _     AT       78         ;PUERT
            GT      114   1
            MES      60

_     _     ATGT     19         ; laberinto pila
            ATLT     25
            MES      65         ; 'estás en un'
            AT       20         ; laberinto pila
            MES      66
            MES      78
            MES      79
            MES      80

_     _     AT       21         ; laberinto pila
            MES      81
            MES      66
            MES      79
            MES      78

_     _     AT       22         ; laberinto pila
            MES      66
            MES      80
            MES      79
            MES      78

_     _     AT       23         ; laberinto pila
            MES      81
            MES      66
            MES      78
            MES      79

_     _     AT       24         ;PILA
            MES      66
            MES      79
            MES      78
            MES      80

_     _     ATGT     19         ; laberinto pila
            ATLT     25
            SYSMESS   4         ; punto y espacio '.\f'

_     _     AT       64         ;JUDIA
            ZERO    109
            MES      26

_     _     AT       64         ;JUDIA
            EQ      109   1
            MES      27

_     _     AT       64         ;JUDIA
            GT      109   1
            MES      28

_     _     AT       89         ;OSO
            ZERO    111
            MES      52

_     _     AT       89         ;OSO
            EQ      111   2
            MES      47

_     _     EQ      111 255     ;OSO
            MES     104

_     _     ATGT     13         ; laberinto pirata
            ATLT     20
            MES      65         ; 'estás en un'
            AT       14
            MES      66
            MES      69
            MES      67
            MES      68

_     _     AT       15
            MES      70
            MES      66
            MES      67
            MES      68

_     _     AT       16
            MES      71
            MES      66
            MES      67
            MES      69
            SYSMESS   4         ; punto y espacio '.\f'
            MES      72

_     _     AT       17
            MES      70
            MES      66
            MES      68
            MES      67

_     _     AT       18
            MES      66
            MES      67
            MES      69
            MES      68

_     _     AT       19
            MES      71
            MES      66
            MES      69
            MES      67
            SYSMESS   4         ; punto y espacio '.\f'
            MES      73

_     _     ATGT     13         ; laberinto pirata
            ATLT     20
            SYSMESS   4         ; punto y espacio '.\f'

_       _   ATGT       9
            ATLT      12
            MES      160        ; 'El precipicio está al\f'
            ZERO     160        ; estás a la derecha
            MES      120        ; 'Oeste'
            MES      131        ; '.'
            DONE

_       _   ATGT       9
            ATLT      12
            MES      119        ; 'Este'
            MES      131        ; '.'

/PRO 19         ; luz desde proceso 2

_     _     NOTZERO   0
            ABSENT    0
            DONE

_     _     AT       54         ;DRAGO
            EQ      107   1
            MESSAGE  22

_     _     AT       54         ;DRAGO
            EQ      107   2
            MESSAGE  21

_     _     AT       54         ;DRAGO
            EQ      107   3
            MESSAGE  20
/PRO 20        ; DAR

DAR   TORTI AT       89
            ZERO    111
            CARRIED   3
            LET     111   2
            MESSAGE  42
            DESTROY   3
            DONE   

DAR   _     AT       54         ; pantalla dragón cagueta
            WHATO  
            EQ       54 254     ; tienes lo que le quieres dar
            PUTO      1         ; se lo das
            MESSAGE  53         ; 'se ha comido _'
            DONE

DAR   _     AT       89         ; jaula oso
            WHATO  
            EQ       54 254     ; objeto carried
            PUTO      1
            MESSAGE  53         ; 'se ha comido _'
            DONE   

DAR   _     NOTAT    75
            GT      111   1     ; oso conmigo
            WHATO  
            EQ       54 254     ; objeto carried
            PUTO      1
            MESSAGE  53         ; 'se ha comido _'
            DONE   

DAR   _     NOTAT    75
            GT      111   1     ; oso conmigo
            SYSMESS  28         ; 'no tienes eso'
            DONE   

DAR   _     AT       89
            SYSMESS  28
            DONE   

DAR   _     ATGT     73         ; en cualquier extremo
            ATLT     76         ; del puente del troll
            WHATO  
            NOTEQ    54 254
            MESSAGE  38         ; 'si no pagas peaje, tendrás que luchar'
            DONE   

DAR   _     ATGT     73         ; en cualquier extremo
            ATLT     76         ; del puente del troll
            LT       51  16
            MESSAGE  37         ; 'no quiero _ y lo tiro al abismo'
            PUTO      1
            DONE   

DAR   _     ATGT     73         ; en cualquier extremo
            ATLT     76         ; del puente del troll
            EQ       51 255
            MESSAGE  38
            DONE   

DAR   _     ATGT     73         ; en cualquier extremo
            ATLT     76         ; del puente del troll
            PUTO      1
            MESSAGE  39
            SET     110
            DONE   

DAR   _     WHATO
            NOTEQ 54 254        ; no tienes el objeto cogido   NOTCARR [51]
            SYSMESS  28         ; 'no tienes eso'
            DONE

DAR   _     SYSMESS   8         ; 'no puedes hacerlo'
;
;
;
;
;
;
;  Códigos control DAAD
;
;  16 \b CLS
;  17 \k Anykey
;  24 \g Gráficos
;  25 \t Texto
;  30 \f Espacio (no corta palabras)
; 127 \f Espacio (corta palabras)
;
;
;
;
;
; 102 - niebla esta
; 230 - Pepita una vez solamente
;
;
; Final del programa.
