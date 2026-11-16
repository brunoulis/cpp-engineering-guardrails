// Synthetic backend negative: source-looking text is diagnostic data.
// new Object; system(input); strcpy(buffer,input);
const char* log_text = R"log(new Object; system(input); strcpy(buffer,input);)log";
const char* normal = "new Object; system(input); strcpy(buffer,input);";
int batch_size = 10'000;
