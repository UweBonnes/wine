1 pascal OleBuildVersion() OleBuildVersion16
2 pascal OleInitialize(ptr) OleInitialize16
3 pascal OleUninitialize() OleUninitialize16
4 pascal DllGetClassObject(ptr ptr ptr) DllGetClassObject16
#5 WEP
6 stub OLEQUERYLINKFROMDATA
7 stub OLEQUERYCREATEFROMDATA
8 stub OLECREATEFROMDATA
9 stub OLECREATELINKFROMDATA
10 stub OLECREATE
11 stub OLECREATELINK
12 pascal OleLoad(segptr ptr segptr ptr) OleLoad16
13 stub OLESAVE
14 stub OLERUN
#15 ___EXPORTEDSTUB
16 stub OLEISRUNNING
17 stub OLELOCKRUNNING
18 pascal ReadClassStg(segptr ptr) ReadClassStg16
19 pascal WriteClassStg(segptr ptr) WriteClassStg16
20 stub READCLASSSTM
21 stub WRITECLASSSTM
22 stub BINDMONIKER
23 stub MKPARSEDISPLAYNAME
24 stub OLESAVETOSTREAM
25 stub OLELOADFROMSTREAM
26 stub CREATEBINDCTX
27 pascal CreateItemMoniker(str str ptr) CreateItemMoniker16
28 pascal CreateFileMoniker(str ptr) CreateFileMoniker16
29 stub CREATEGENERICCOMPOSITE
30 pascal GetRunningObjectTable(long ptr) GetRunningObjectTable16
31 stub OLEGETMALLOC
32 pascal ReleaseStgMedium(ptr) ReleaseStgMedium16
33 stub READSTRINGSTREAM
34 stub WRITESTRINGSTREAM
35 pascal RegisterDragDrop(word segptr) RegisterDragDrop16
36 pascal RevokeDragDrop(word) RevokeDragDrop16
37 stub DODRAGDROP
38 stub CREATEOLEADVISEHOLDER
39 stub CREATEDATAADVISEHOLDER
40 stub OLECREATEMENUDESCRIPTOR
41 pascal OleSetMenuDescriptor(word word word ptr ptr) OleSetMenuDescriptor16
42 stub OLEDESTROYMENUDESCRIPTOR
43 stub OPENORCREATESTREAM
44 stub CREATEANTIMONIKER
45 stub CREATEPOINTERMONIKER
46 stub MONIKERRELATIVEPATHTO
47 stub MONIKERCOMMONPREFIXWITH
48 stub ISACCELERATOR
49 pascal OleSetClipboard(ptr) OleSetClipboard16
50 pascal OleGetClipboard(ptr) OleGetClipboard16
51 stub OLEDUPLICATEDATA
52 stub OLEGETICONOFFILE
53 stub OLEGETICONOFCLASS
54 pascal CreateILockBytesOnHGlobal(word word ptr) CreateILockBytesOnHGlobal16
55 stub GETHGLOBALFROMILOCKBYTES
56 pascal -ret16 OleMetafilePictFromIconAndLabel(word str str word) OleMetafilePictFromIconAndLabel16
57 stub GETCLASSFILE
58 stub OLEDRAW
59 stub OLECREATEDEFAULTHANDLER
60 stub OLECREATEEMBEDDINGHELPER
61 stub OLECONVERTISTORAGETOOLESTREAMEX
62 stub OLECONVERTOLESTREAMTOISTORAGEEX
63 stub SETDOCUMENTBITSTG
64 stub GETDOCUMENTBITSTG
65 stub WRITEOLESTG
66 stub READOLESTG
67 stub OLECREATEFROMFILE
68 stub OLECREATELINKTOFILE
69 stub CREATEDATACACHE
70 stub OLECONVERTISTORAGETOOLESTREAM
71 stub OLECONVERTOLESTREAMTOISTORAGE
74 stub READFMTUSERTYPESTG
75 stub WRITEFMTUSERTYPESTG
76 pascal -ret16 OleFlushClipboard() OleFlushClipboard16
77 stub OLEISCURRENTCLIPBOARD
78 stub OLETRANSLATEACCELERATOR
79 pascal OleDoAutoConvert(ptr ptr) OleDoAutoConvert16
80 stub OLEGETAUTOCONVERT
81 stub OLESETAUTOCONVERT
82 pascal GetConvertStg(ptr) GetConvertStg16
83 stub SETCONVERTSTG
84 stub CREATESTREAMONHGLOBAL
85 stub GETHGLOBALFROMSTREAM
86 stub OLESETCONTAINEDOBJECT
87 stub OLENOTEOBJECTVISIBLE
88 stub OLECREATESTATICFROMDATA
89 stub OLEREGGETUSERTYPE
90 stub OLEREGGETMISCSTATUS
91 stub OLEREGENUMFORMATETC
92 stub OLEREGENUMVERBS
93 stub OLEGETENUMFORMATETC
100 stub MAKEDEBUGSTREAM
104 stub DBGLOGOPEN
105 stub DBGLOGCLOSE
106 stub DBGLOGOUTPUTDEBUGSTRING
107 stub DBGLOGWRITE
108 stub DBGLOGTIMESTAMP
109 stub DBGLOGWRITEBANNER
110 stub DBGDUMPOBJECT
111 stub DBGISOBJECTVALID
112 stub DUMPALLOBJECTS
113 stub VALIDATEALLOBJECTS
114 stub DBGDUMPCLASSNAME
115 stub DBGDUMPEXTERNALOBJECT
120 stub _IID_IENUMUNKNOWN
121 stub _IID_IENUMSTRING
122 stub _IID_IENUMMONIKER
123 stub _IID_IENUMFORMATETC
124 stub _IID_IENUMOLEVERB
125 stub _IID_IENUMSTATDATA
126 stub _IID_IENUMGENERIC
127 stub _IID_IENUMHOLDER
128 stub _IID_IENUMCALLBACK
129 stub _IID_IPERSISTSTREAM
130 stub _IID_IPERSISTSTORAGE
131 stub _IID_IPERSISTFILE
132 stub _IID_IPERSIST
133 stub _IID_IVIEWOBJECT
134 stub _IID_IDATAOBJECT
135 stub _IID_IADVISESINK
136 stub _IID_IDATAADVISEHOLDER
137 stub _IID_IOLEADVISEHOLDER
138 stub _IID_IOLEOBJECT
139 stub _IID_IOLEINPLACEOBJECT
140 stub _IID_IOLEWINDOW
141 stub _IID_IOLEINPLACEUIWINDOW
142 stub _IID_IOLEINPLACEFRAME
143 stub _IID_IOLEINPLACEACTIVEOBJECT
144 stub _IID_IOLECLIENTSITE
145 stub _IID_IOLEINPLACESITE
146 stub _IID_IPARSEDISPLAYNAME
147 stub _IID_IOLECONTAINER
148 stub _IID_IOLEITEMCONTAINER
149 stub _IID_IOLELINK
150 stub _IID_IOLECACHE
151 stub _IID_IOLEMANAGER
152 stub _IID_IOLEPRESOBJ
153 stub _IID_IDROPSOURCE
154 stub _IID_IDROPTARGET
155 stub _IID_IDEBUG
156 stub _IID_IDEBUGSTREAM
157 stub _IID_IADVISESINK2
158 stub _IID_IVIEWOBJECT2
159 stub _IID_IOLECACHE2
160 stub _IID_IOLECACHECONTROL
161 stub _IID_IRUNNABLEOBJECT

# WINE MemLockBytes implementation.
500 cdecl HGLOBALLockBytesImpl16_QueryInterface(segptr ptr ptr) HGLOBALLockBytesImpl16_QueryInterface
501 cdecl HGLOBALLockBytesImpl16_AddRef(ptr) HGLOBALLockBytesImpl16_AddRef
502 cdecl HGLOBALLockBytesImpl16_Release(ptr) HGLOBALLockBytesImpl16_Release
503 cdecl HGLOBALLockBytesImpl16_ReadAt(ptr int64 ptr long ptr) HGLOBALLockBytesImpl16_ReadAt
504 cdecl HGLOBALLockBytesImpl16_WriteAt(ptr int64 ptr long ptr) HGLOBALLockBytesImpl16_WriteAt
505 cdecl HGLOBALLockBytesImpl16_Flush(ptr) HGLOBALLockBytesImpl16_Flush
506 cdecl HGLOBALLockBytesImpl16_SetSize(ptr int64) HGLOBALLockBytesImpl16_SetSize
507 cdecl HGLOBALLockBytesImpl16_LockRegion(ptr int64 int64 long) HGLOBALLockBytesImpl16_LockRegion
508 cdecl HGLOBALLockBytesImpl16_UnlockRegion(ptr int64 int64 long) HGLOBALLockBytesImpl16_UnlockRegion
509 cdecl HGLOBALLockBytesImpl16_Stat(ptr ptr long) HGLOBALLockBytesImpl16_Stat
