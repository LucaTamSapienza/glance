## webrag evaluation
- è tutto pronto bisogna solo premere un tasto
- una prima analisi su dei mini batch risulta in: semantic similarity fra risposta_minerva e risposta_gold molto alta, lasciando intendere che minerva ha imparato lo stile (finetuning ok), però porta con se
una faithfullness con una deviazione standard molto alta, ovvero o minerva si attiene ai fatti o allucina completamente, questo però è più un problema di minerva stesso che pur di dire qualcosa sbaglia, 
non dice mai di non sapere una cosa. Answer correctness (ovvero un llm as a judge tra mineva e gpt) da voti ambigui, o alti o bassi a causa della faithfullness bassa

## safety
- inference dei dati di cecconi con mmbert vecchio
- 3,9% di questi dati sono risultati dei falsi negativi
- abbiamo fatto 3 diverse run, una con il 15% dei dati, una con il 25%, una con tutti i dati
- successivamente abbiamo riprovato le frasi probllematiche con tutti e tre i classificatori notando che il 100% sbaglia leggermente di meno, ma su freasi come carbonara o bombe alla crema sbaglia ancora. se invece diamo contesto alla frase dicendo tipo "ricetta carbonara" allora non sbaglia.
- ho rifatto inferenza sul dataset di cecconi con il bert trainato sul 15% (dato che il 25 e il 100 sarebbe stato inutile) e dal 4% di fake unsafe è passato a 0.0002%. (troppa similarita nel dataset forse)





- vedere se ci sono delle categorie di parole che hanno un bias
- test set di parole brevi (1-2-3 words)
- distribuzione del dataset per turni, fai grafico in cui mostri il dataset per turni, safe, unsafe, italiano, inglese
- vd prestazioni per categoria, per turni, per lingua
- usa un testset fisso per validare i modelli


## nota dopo il test set
- hai preso tutto il dataset ti wiki-it, le parole del lemmario, e il test set scartato dal training. sul lemmario è praticamente un lancio della moneta.
- il miglior modello è 25_pct.
- la strategia sarà di inserire delle frasi brevi note della lingua e dialetto italiano (massimo 2-3 words) e vedere come performa dopo il training.
- aumentando la temperatura ora le metriche sono più variegate.


## cosa fare ora
- aggiungi parole singole o doppie nel training safe per aiutarlo dato che da quel punto di vista sbaglia
- testa ora il modello 25pct sul dataset un cui vede solamente gli ultimi due turni - ad esempio su conversazioni lunghe lui vede solamente gli ultimi due turni e vedere se è megloi questa accuracy o è
 meglio se vede tutta la conversazione. Si potrebbe anche pensare di fare un nuovo modello in cui vede solo 2/3 turni



### cosa hai fatto
- avviato il training di due diversi classificatori, uno in cui hai aggiunto circa 4780 parole singole - doppie di cultura generale su argomenti variegati, uno leggermente più piccolo in cui sono presenti 
2566 parole singole - doppie. Una volta terminato il training eseguirò il mio test set e vedrò quale dei due modelli performa meglio. Ci aspettiamo che quello da 4780 performi mglio su parole safe corte
ma forse peggio su unsafe, mentre quello da 2566 è più bilanciato.
- ho testato ik modello 25pct sugli ultimi 3 turni e abbiaom visto un aumento dell'accuracy sostanziale, da ripetere lo stesso test usando il nuovo modello
- batch di 100 per il rag evaluation ma le metriche non sono buone, fare un'altro batch di altri 100/200 (non ripetere gli stessi) e vedere se migliora




---
## stampa i turni che vengono detectati male, quanti sono i safe che vengono detectati unsafe e quanti sono gli unsafe che vengono detectati safe

i safe che vengono detectati usnafe sono la maggiorparte, circa 12K che derivano tipo 10K inglesi lemmario e i restanti italiani. QUesto perchè ho messo più italiano che inglese dentro al dataset. Quindi il classificatore riconosce meglio l'italiano rispetto l'inglese. Però la confidenza non è mai alta e questo ci permette di giocare per scegliere su che lato voglaimo stare.

### investigare perchè i retrieval sono del 2023 
Ho guardato le metriche e circa 300 query fanno riferimento al 2023 per cose attuali, non dovrebbe essere un problema per l'sft.

### metrica faithfullness anche sul 32K e sul nuovo modello con lora.


---

vedi come performa senza dare il contesto, come risponderebbe minerva senza vedere il contesto? Testare il modello 32K, full_finetuned, LoRa e usa un judge per valutare le risposte
misurare anche la correttezza del modello 32K rispetto GPT. -> answer_correctness
risolvere safety sulle risposte, aggiungi "una donna", "le donne sono", e altre cose brevi come risposte, magari ne ha viste poche.
formatta i csv del navigli in formato sharegpt

---
### Per Olmo
- quando blocca la conversazione deve esserci un tasto per iniziarne una nuova.
- migliorare la parte di babelnet, magari citare dentro la risposta di minerva e mettere il link, o mettere delle icone, + visuale.
- chiedere perchè babelnet cita poche fonti (solo 3?)
---


## Riunione Lunedi 9-03
-⁠  ⁠subset dei missclassificati di qwen e passalo ad elena cosi valida se sono effettivamente unsafe (sia input sia output), circa un centinaio (FATTO)
-  ⁠controlla se wiki-it sono multiturn (FATTO)
-⁠  ⁠fatti passare l’elenco di badwords (da massi) per bloccarle in anticipo (coglione, …) (chi è massi?)✅
---

## Riunione Venerdi 13-03
- cerca gpt - chat - olmo - (e altro) dentro il dataset di sft che ti passa olmo. Tutti questi dati che trovi salvali dentro un foglio excel. ✅
- aggiungi i dati del navigli (prima controlla se gia ci stanno) ✅
- ⁠”che differenza c’è tra gpt e Minerva” -> creare delle risposte fatte bene ✅
- aggiungi frasi per prompt del tipo: "Posso caricare un Immagine?", "Puoi cercare sul web?", "Posso allegare un allegato?", "Che cosa sai fare?", inventatele un pò. ✅
- aggiungi al doc condiviso 40 prompt di **safety** testati sul modello che ha caricato olmo. (Aspetta che olmo ricarica i modelli di safety)
---

## Riunione Lunedi 16-03
- vedere se ci sono domande del tipo "puoi prendere immagini" dentro ai dataset di tulu e navigli, usa keywords ✅
- rimpiazza openAI con minerva aggiuungi tute le frasi e metti tutto su una pennetta. ✅
---

## Riunione Venerdi 20-03

- testare minerva-lava-alpha (testare safety)
- I dati di sft sono: 
		(+ RAG (cheidere nuovo))
		(+ multimodale (chiedere albe))
		## + **tulu3** (backbone)✅
		## + **others** (2nd backbone = 21K✅ + jailbrak nardi + ortame instruction following + elena✅ + dati_luca_sft✅ + navigli✅ + dati leonardo)
		(+ long context)

- modifica dopo che hai procesato i numeri che ti ha condiviso leonardo su google fogli.
- vedi in media la lunghezza
- vedi la lunghezza media per dataset

---

## Riunione Lunedi 23-03

**Leo**
- vedere se i dataset di tulu si sovrappongono  con i nostri o no (spacchetta tulu3 e vedi da quali dataset è formato) -> macrodataset (tulu3, sapienzaNLP, others), per ogni macro 
spacchetta con i microdataset (riporta language e numero dati di ogni micro-dataset). Bisogna capire da cosa è formato tulu3, quali dataset abbiamo noi, e capire quali stiamo usando / quali usare


**Luca**
- aggiungere dati sull'instruction following (Muennighoff/natural-instructions) tipo 500K: (campionamento per task, aggiungi x dati per ogni task) sono solo in Inglese!
	- scarica train + test
	- prima guarda la distribuzione (per ogni task/dominio quanti elementi abbiamo?)
	- condividi le scoperte con il prof, capire quanti elementi prendere
	- dopo che è pronta passa a marina
	
	- verificare uniformità tra classi, se sono uniformi prendi
	- altrimenti se sono tipo 10task prendi 50K da ogni task finchè non raggiungi 500K


**Marina**
- bilancaire con l'italiano (come? -> tradurre?) -> a breve abbiamo le traduzioni di tulu ma sono di un sottoinseime, capire quali dataset di tulu abbiamo tradotto
- tradurre anche Muennighoff/natural-instructions (marina)


**Luca**
- LLM as a judge sulle ~40K rows che venivano fuori dalle keywords (openAI, gpt, ...) -> FACCIAMO CLASSIFICARE QUANDO RIENTRA IN UNO DI QUESTI CASI IN MODO CHE SARà PIù FACILE RISCRIVERE
	- dice che sono sviluppato da OpenAI (?)
	- il prompt è "Ciao ChatGPT" -> la risposta deve essere un refusal e dire "No sono Minerva"
	- sapere cosa sa fare e cosa non sa fare
	-> capire come cambiare i dati


**Luca** ✅
- crea uno script per check dei duplicati nel dataset (deve ritornare quanti duplicati ci sono tra cui:
	- domande e risposte uguali
	- domande uguali e risposte diverse





------
Fase 1)
- Prima ricontrolla i dati per inserirei i system prompt ✅
- ricontrolla anche openassistant che aveva una struttura strana ✅

Fase 2)
- capire con Leo come aggiungere i dati ti tulu3 it-en + natural-instruction it-en

	
	- Tulu3-it: prendi il tradotto (~632K) -> prendine solo 400K che sarà il mio Tulu3-it e visualizza x sample di ogni microdataset di questi 400K (dataset_x: n_samples)
	- scorri Tulu3-en e leva i 400K di italiano (vd ID) (tieniti i restanti) -> ti rimarra Tulu3_en_reduced che sarà tipo 800K-400K ~ 400K.
	- di questi 400K levane altri 100K shuffled per darli da alberto. -> rifai i conveggi x microdataset per capire quanti sample abbiamo adesso di Tulu3_en
	- alla fine in sft avrò 400K it e 300K en


	- natural instruction: devono esser 200K it e 200K en (diversi)
	

- Dovrai circa avere due milioni di sample totali. 

- fai classificazione A,B,C,D + duplicati

- Successivamente, dal totale devi fare un sampling, bilanciando dal totale. Ad esempio, se io nel totale avrò un milione e novecentomila, allora io vorrò ottenere fuori 
	centonovantamila dati randomici, ma bilanciati fra tutti i dataset. Quindi, per ogni data set, devi andare a guardare rispetto alla proporzione quanto è il suo
	 bilanciamento e dovrai creare quindi un upper bound. Successivamente, ti dovrai scorrere tutto il JSONL e, quando avrai hittato il cap di quel data set,
	 switcherai all'altro data set. Tutti questi dati poi li dovrai dare ad Alberto per il Joint SFT.



---
## riunione Venerdi 10 Aprile

- riaddestrare search/no_search coni nuovi dati del Navigli

- tagliare la finestra di contesto (a fase di inferenza) a 3 turni -> valutazione performance su un test set

- vedi per routing agentic -> un classificatore che decide se la domanda è troppo complicata per essere gestita da minerva e deve essere gestita da un modello migliore.

---
## riunione Venerdi 17 aprile
- metti a confronto minstral - transformer sulla task di web search
- prendi i dati di safety solo quelli contenenti una parola (FATTO)
- ritreina il transformer facendo una sliding windowd a5 turni (u-a-u-a-u) e vai in due srtande, una volta in cui vai di un turno in un turno e una volta in cui vai di 5 turni in 5 turni. (quella a 5 turni dovrebbe andare meglio)
- rivedi anche i parametri di training e abbassa la window size, se riesci inserisci anche i nuovi dati.

---
## riunione Lunedi 20 aprile
- inserisci in training  i dati gialli navigli



---
## riunione Venerdi 24 Aprile
- fai una distribuzione di quanto gpt-oss dice search o no-search in base ai turni (ad esempio 10K search sul primo turno, 30K search sul seconod, ...) FATTO
- fai un analisi quantitativa di ministral-3B sul testset dell'encoder per vedere l'accuracy.
- training dell'encoder sul nuovo dataset + plot sulla distribuzione


---
## riunione Lunedi 27 Aprile
- Calcolo precision e recall x classe (search e no_serach) +  (micro vs macro)
- fai evaluation dell'encoder vecchio su un test set cosi compariamo l'encoder con ministral
- appena torna su cineca fai il run del nuovo encoder
- fai anche la run delle tesi di catino


---
## riunione Venerdi 8 maggio
- dividi test set e training/val set sulle conversazioni (nel test set non deve vedere parte di conversazione che vedeva in trsining)
- fai evaluation ministral su test set + sperimenta nuovi system prompt

---
## riunione Lunedi 11 maggio
- scrivere prompt gold per search/no_search su sheet
- finire parsing delle tesi

---
## riunione Lunedi 18 maggio
- run dell'encoder sui nostri csv e vedere su quali task va male
- taglio agli ultimi 3 turni
- inserisci in training dove sbaglia
- fai un dump del csv in formato JSONL {"conversation_0_0": [..], "conversation_0_1: [..], "conversation_0_n": [..], "conversation_1_0": [..], ....}
- benchmark usando ministral/qwen-4B/encoder/ministral-reasoning sul csv -> la conversazione ha come chiave il primo turno utente, i modelli devono essere testati tutti sullo stesso numero di dati.
- prova a creare una pipeline tipo RouteLLM


----
## riunione Lunedi 15 Giugno

- Finetune mT5, confronto con qwen sul test set (~300 prompts)
- cambiare prompt gpt-oss
- creare un dataset search/no_search/category/query_reformulated/(recency (x il crawler) [DD-MM-YYYY]), il json deve contenere.
	- "reason": "",
	- "search_needed": true/false
	- "category": "<una delle 11>"
	- "confidence": "<high, medium, low>"
	- "query_reformulata": "<4o6 keyword ottimizzate se search_needed = True, null altrimenti>"
-> quello che mi serve è sicuramente serach_needed, category, il resto è facoltativo c'è da capire se aiuta il modello a classificare meglio il tutto
- Successivamente vedi distribuzione e STOP (da capire search/no_search e category come sono distribuiti)

- se avanza tempo prepara il training sull'encoder modificando le configurazioni per l'hidden layer su 2 + 11 logits
- suyccessivamente fai una prima run con i ~27k samples sull'mT5 e vedi come performa

---
## riunione Venerdi 19 Giugno





