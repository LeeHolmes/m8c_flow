#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include <signal.h>

#include "flite/include/flite.h"

cst_voice *flite_voice;
int initializing_flite = 0;

extern char screenbuffer[24][40];
extern char selection_buffer[24][40];

int selection_row;
extern int new_selection_row;
int selection_column;

char current_page[40];
char current_selection[40];

cst_lexicon *cmulex_init(void);
void usenglish_init(cst_voice *v);

pthread_t current_flite_thread;
int currently_speaking = 0;

void* flite_thread(void* phrase) {
  currently_speaking = 1;
  flite_text_to_speech((char*) phrase, flite_voice, "play");
  currently_speaking = 0;
  return NULL;
}

// Speak the given phrase in a background thread so that we
// can interrupt it when it is speaking and the user switches selections
// or pages.
char current_phrase[500];
void flite_speak(char* phrase) {

  if(currently_speaking)
  {
    pthread_cancel(current_flite_thread);
  }

  strcpy(current_phrase, phrase);
  pthread_create(&current_flite_thread, NULL, flite_thread, current_phrase);
}

void dump_screenbuffer() {

  // Header column of 10s
  printf("    ");
  for(int col = 0; col < 40; col++)
  {
    if(col % 10 == 0)
    {
      printf("%d", col / 10);
    }
    else
    {
      printf(" ");
    }
  }
  printf("\n");

  // Header column of 1s
  printf("    ");
  for(int col = 0; col < 40; col++)
  {
    printf("%d", col % 10);
  }
  printf("\n");

  // Row contents
  for(int row = 0; row < 24; row++)
  {
    printf("%2d: %s\n", row, screenbuffer[row]);
  }
}

char* trim(char *str)
{
  char *end;

  // Trim leading space
  while(isspace((unsigned char)*str)) str++;

  if(*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) end--;

  // Write new null terminator character
  end[1] = '\0';

  return str;
}

void speak_flow() {

  // If we haven't initialized flite, initialize it now
  if(flite_voice == NULL)
  {
    if(initializing_flite) { return; }
    initializing_flite = 1;

    printf("Initializing Flow Mode.\r\n");
    flite_init();
    flite_add_lang("eng", usenglish_init, cmulex_init);
    flite_add_lang("usenglish", usenglish_init, cmulex_init);    
    printf("Loading voice.\r\n");
    flite_voice = flite_voice_select("file://cmu_us_fem.flitevox");
    printf("Loaded.\r\n");
  }

  // Get the current page title
  char* new_page = (char *)calloc(40, sizeof(char));

  strcpy(new_page, screenbuffer[2]);
  char* new_page_adjusted = trim(new_page);

  // Parse sub-pages
  if(strstr(screenbuffer[8], "ENV1 TO"))
  {
      strcat(new_page_adjusted, " EFFECTS");
  }

  // Get the current selection
  int selection_changed = 0;
  char new_selection[40] = "";
  sscanf(selection_buffer[new_selection_row], "%s", new_selection);
  
  int new_selection_column = -1;
  if(strlen(new_selection) != 0)
  {
    char* search_result = strstr(selection_buffer[new_selection_row], new_selection);
    new_selection_column = search_result - selection_buffer[new_selection_row];

    if(    
        (strcmp(current_selection, new_selection) != 0) ||
        (selection_column != new_selection_column) ||
        (selection_row != new_selection_row))
    {
        selection_changed = 1;

        strcpy(current_selection, new_selection);
        selection_column = new_selection_column;
        selection_row = new_selection_row;
    }
  }

  // If nothing has changed, don't speak anything new.
  if((strcmp(current_page, new_page_adjusted) == 0) && (! selection_changed))
  {
    free(new_page);
    return;
  }

  dump_screenbuffer();
  
  char* guidance = (char*) calloc(1000, sizeof(char));

  // Include page transitions
  if(strcmp(current_page, new_page_adjusted) != 0)
  {
    strcpy(current_page, new_page_adjusted);
    strcat(guidance, current_page);
    strcat(guidance, " . ");
  }
  free(new_page);

  // Determine the current row and column
  int column_heading_column = new_selection_column;
  int row_heading_column = 0;

  // Ajust the heading column in the FX region of the
  // phrase page, since the alignment is non-standard
  int is_value = 0;
  if(strstr(screenbuffer[2], "PHRASE"))
  {
    if((new_selection_column == 15) ||
       (new_selection_column == 21) ||
       (new_selection_column == 27))
    {
        column_heading_column -= 3;
        is_value = 1;
    }
  }

  // Adjust the row headings for the multi-column
  // instrument page
  int is_instrument = 0;
  if(strstr(screenbuffer[2], "INST"))
  {
    is_instrument = 1;
    if(new_selection_column == 22) { row_heading_column = column_heading_column - 4; }
    if(new_selection_column == 27) { row_heading_column = column_heading_column - 10; }

    // See if this is an instrument value with a description
    if(isalpha(screenbuffer[new_selection_row][new_selection_column + 2]))
    {
        char instrument_description[40];
        sscanf(screenbuffer[new_selection_row] + new_selection_column + 2, "%s", instrument_description);
        strcat(current_selection, " ");
        strcat(current_selection, instrument_description);
    }
  }

  // Determine current row and column
  char row_heading[40], column_heading[40];
  sscanf(screenbuffer[new_selection_row] + row_heading_column, "%s", row_heading);
  sscanf(screenbuffer[4] + column_heading_column, "%s", column_heading);

  // Include current selection
  strcat(guidance, current_selection);

  // Include context
  if(is_instrument)
  {
    if((strcmp(current_selection, "LOAD") == 0) ||
      (strcmp(current_selection, "SAVE") == 0))
    {
      strcpy(row_heading, "INSTRUMENT");
    }

    strcat(guidance, " ");
    strcat(guidance, row_heading);
  }
  else
  {
    strcat(guidance, " . at row ");
    strcat(guidance, row_heading);
    strcat(guidance, " column ");
    strcat(guidance, column_heading);
  }

  // If this is the value for a column with a name,
  // add that to the spoken phrase.
  if(is_value)
  {
    strcat(guidance, " value");
  }
  
  printf("Current guidance: %s\n", guidance);

  // Fix abbreviations and annoyances
  char final_guidance[1000] = "";
  char current_word[40];
  char* search_start = guidance;
  while(sscanf(search_start, "%s", current_word) > 0)
  {
    int use_original = 1;
    char* found = strstr(search_start, current_word);

    if(found)
    {
        search_start = found +  + strlen(current_word) + 1;

        char* replacements[] = {
            "INST.", "Instrument",
            "MACROSYN", "Macro synth",
            "-", "NO VALUE",
            "--", "NO VALUE",
            "---", "NO VALUE",
            "PH", "Phrase",
            "TSP", "Transpose",
            "N", "Note",
            "V", "Velocity",
            "I", "Instrument",
            "TRANSP.", "Transpose",
            "RES", "Resonance",
            "AMP", "Amplification",
            "LIM", "Limit",
            "CHO", "Chorus",
            "DEL", "Delay",
            "REV", "Reverb",
            "OUTPUT VOL", "Output Volume",
            "SPEAKER VOL", "Speaker Volume",
            "WAV", "Wave",
        };

        // Fix abbreviations
        for(int counter = 0; counter < (sizeof(replacements) / sizeof(replacements[0])); counter += 2)
        {
            if(strcmp(current_word, replacements[counter]) == 0)
            {
                strcat(final_guidance, replacements[counter + 1]);
                use_original = 0;
            }           
        }
        
        // Fix speaking hex (like "FE" sounding like "FEE")
        char* endPtr;
        if((strlen(current_word) == 2) &&
            (isalpha(current_word[0]) || isalpha(current_word[1])))
        {
            strtol(current_word, &endPtr, 16);
            if((endPtr - current_word) == 2)
            {
                char letters[4] = { current_word[0], ' ', current_word[1], 0 };
                strcat(final_guidance, letters);
                use_original = 0;
            }
        }

        if(use_original)
        {
            strcat(final_guidance, current_word);
        }

        strcat(final_guidance, " ");
    }
  }

  printf("Final Guidance: %s\n", final_guidance);
 
  free(guidance);
  flite_speak(final_guidance); 
}