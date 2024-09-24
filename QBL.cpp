#include <iostream>

#include "emp/base/vector.hpp"
#include "emp/config/FlagManager.hpp"
#include "emp/io/File.hpp"
#include "emp/tools/String.hpp"

#include "Question.hpp"
#include "QuestionBank.hpp"

#define QBL_VERSION "0.0.1"

using emp::String;

class QBL {
private:
  QuestionBank qbank;
  emp::FlagManager flags;

  enum class Format {
    NONE=0,
    QBL,
    D2L,
    GRADESCOPE,
    LATEX,
    WEB,
    DEBUG
  };

  enum class Order {
    DEFAULT = 0,
    RANDOM,
    ID,
    ALPHABETIC
  };

  Format format = Format::NONE;       // No format set yet.
  Order order = Order::DEFAULT;       // Don't reorder questions.
  String base_path = "";              // Where are we placing these files?
  String base_filename = "";          // Output filename; empty=no file
  String extension = "";              // Provided extension to use for output file.
  String log_filename = "";           // Where should we log questions to?
  String title = "Multiple Choice Quiz"; // Title to use in any generated files.
  emp::vector<String> include_tags;   // Include ALL questions with these tags.
  emp::vector<String> exclude_tags;   // Exclude ALL questions with these tags (override includes)
  emp::vector<String> require_tags;   // ONLY questions with these tags can be included.
  emp::vector<String> sample_tags;    // Include at least one question with each of these tags.
  emp::vector<String> question_files; // Full set of questions
  emp::vector<String> avoid_files;    // Files with lists of questions IDs to avoid
  size_t generate_count = 0;          // How many questions should be generated? (0 = use all)
  emp::Random random;                 // Random number generator
  bool compressed_format = false;     // Should GradeScope output be compressed?

  // Helper functions
  void _AddTags(emp::vector<String> & tags, const String & arg, size_t count=1) {
    auto args = arg.Slice();
    for (size_t i = 0; i < count; ++i) {
      emp::Append(tags, args);
    }
  }

public:
  QBL(int argc, char * argv[]) : flags(argc, argv) {
    flags.AddGroup("Basic Operation",
      "These flags are the standard ones to use when running QBL.\n");
    flags.AddOption('g', "--generate",[this](String arg){ SetGenerate(arg); },
      "Randomly generate questions (number as arg).");
//    flags.AddOption('I', "--interactive",     [this](){},
//      "Start QBL in interactive mode for more dynamic exam generation.");
    flags.AddOption('o', "--output",  [this](String arg){ SetOutput(arg); },
      "Set output file name [arg].");
    flags.AddOption('S', "--seed", [this](String arg){ SetRandomSeed(arg); },
      "Set the random number seed with the following argument [arg]");
    flags.AddOption('t', "--title", [this](String arg){ SetTitle(arg); },
      "Specify the quiz/exam title to use in the generated file.");

    flags.AddGroup("Output Format",
      "These flags specify the output format to use.  If none are provided, the\n"
      "extension on the output filename is used, or else QBL format is the default.\n");
    flags.AddOption('d', "--d2l",     [this](){ SetFormat(Format::D2L); },
      "Set output to be D2L / Brightspace csv quiz upload format.");
    flags.AddOption('G', "--gradescope",     [this](){ SetFormat(Format::GRADESCOPE); },
      "Set output to be in Latex format suitable for using with GradeScope.");
    flags.AddOption('l', "--latex",   [this](){ SetFormat(Format::LATEX); },
      "Set output to be Latex format.");
    flags.AddOption('q', "--qbl",     [this](){ SetFormat(Format::QBL); },
      "Set output to be QBL format.");
    flags.AddOption('w', "--web",     [this](){ SetFormat(Format::WEB); },
      "Set output to HTML/CSS/JS format.");
    flags.AddOption('O', "--order",   [this](String arg){ SetOrder(arg); },
      "Set the question order based on [arg] (\"random\", \"id\", or \"alpha\")");
    flags.AddOption('c', "--compressed",   [this](){ compressed_format = true; },
      "Make questions take less space (only works for GradeScope output).");

    flags.AddGroup("Question Specification",
      "These options provide addition constraints as QBL decides which questions\n"
      "should or should not be used in the output.\n");
    flags.AddOption('i', "--include", [this](String arg){ _AddTags(include_tags, arg); },
      "Include ALL questions with the following tag(s), not otherwise excluded.");
    flags.AddOption('r', "--require", [this](String arg){ _AddTags(require_tags, arg); },
      "Only questions with the following tag(s) can be included.");
    flags.AddOption('s', "--sample",
      [this](String tag_arg, String count_arg){ _AddTags(sample_tags, tag_arg, count_arg.As<size_t>()); },
      "Specify tag(s) and the number of times they should be included.");
    flags.AddOption('x', "--exclude", [this](String arg){ _AddTags(exclude_tags, arg); },
      "Exclude all questions with following tag(s).");
    flags.AddOption('L', "--log", [this](String arg){ log_filename = arg; },
      "Log the IDs of the questions chosen to the file [arg].");
    flags.AddOption('a', "--avoid", [this](String arg){ avoid_files.push_back(arg); },
      "Provide a filename ([arg]) to avoid questions from; can previously be generated as log.");
    

    flags.SetGroup("none");
 //    flags.AddOption('c', "--command",     [this](){},
 //      "Run a single interactive command; e.g. `var=12`.");
    flags.AddOption('D', "--debug",   [this](){ SetFormat(Format::DEBUG); },
      "Print extra debug information.");
    flags.AddOption('h', "--help",    [this](){ PrintHelp(); },
      "Provide usage information for QBL (this message)");
    flags.AddOption('v', "--version", [this](){ PrintVersion(); },
      "Provide QBL version information.");


    flags.Process();
    question_files = flags.GetExtras();
  }

  void SetTitle(const String & in) { title = in; }

  void SetFormat(Format f) {
    emp::notify::TestWarning(format != Format::NONE,
      "Setting format to '", GetFormatName(f),
      "', but was already set to ", GetFormatName(format), ".");
    format = f;
  }

  void SetOutput(String _filename, bool update_ok=false) {
    if (base_filename.size() && !update_ok) {
      emp::notify::Error("Only one output mode allowed at a time.");
      exit(1);
    }
    std::cout << "Directing output to file '" << _filename << "'." << std::endl;
    size_t slash_pos = _filename.RFind('/');
    if (slash_pos != emp::String::npos) {
      if (slash_pos+1 == _filename.size()) {
        emp::notify::Error("Must provide a filename (not directory) for output.");
        exit(1);
      }
      base_path = _filename.PopFixed(slash_pos+1);
    }
    size_t dot_pos = _filename.RFind('.');
    base_filename = _filename.substr(0, dot_pos);
    extension = _filename.View(dot_pos);
    // If we don't have a format yet, set it based on the filename.
    if (format == Format::NONE) {
      if (extension == ".csv" || extension == ".d2l") format = Format::D2L;
      if (extension == ".gscope") format = Format::GRADESCOPE;
      else if (extension == ".html" || extension == ".htm") format = Format::WEB;
      else if (extension == ".tex") format = Format::LATEX;
      else if (extension == ".qbl") format = Format::QBL;
    }
  }

  void SetGenerate(String _count) {
    if (generate_count != 0) {
      emp::notify::Error("Can only set one value for number of questions to generate.");
    }
    generate_count = _count.As<size_t>();
    // If order hasn't been manually set, change it to random.
    if (order == Order::DEFAULT) order = Order::RANDOM;
  }
  
  void SetRandomSeed(String _seed) {
    int random_seed = _seed.As<int>();
    std::cout << "Using random seed: " << random_seed << std::endl;
    random.ResetSeed(random_seed);
  }

  void SetOrder(String _order) {
    if (_order == "random") { order = Order::RANDOM; }
    else if (_order == "id") { order = Order::ID; }
    else if (_order == "alpha") { order = Order::ALPHABETIC; }
    // @CAO - Other options are layout filenames
  }

  void UpdateOrder() {
    switch (order) {
    case Order::DEFAULT:    break; // No changes needed
    case Order::RANDOM:     qbank.Randomize(random); break;
    case Order::ID:         qbank.SortID();          break;
    case Order::ALPHABETIC: qbank.SortAlpha();       break;
    }
  }

  void PrintVersion() const {
    std::cout << "QBL (Question Bank Language) version " QBL_VERSION << std::endl;
  }

  void PrintHelp() const {
    PrintVersion();
    std::cout << "Usage: " << flags[0] << " [flags] [questions_file]\n";
    flags.PrintOptions();
    exit(0);
  }

  String GetFormatName(Format id) const {
    switch (id) {
    case Format::NONE: return "NONE";
    case Format::D2L: return "D2L";
    case Format::GRADESCOPE: return "GRADESCOPE";
    case Format::LATEX: return "LATEX";
    case Format::QBL: return "QBL";
    case Format::WEB: return "WEB";
    case Format::DEBUG: return "Debug";
    };

    return "Unknown!";
  }

  void LoadFiles() {
    for (auto filename : question_files) {
      qbank.NewFile(filename);   // Let the question bank know we are loading from a new file.
      emp::File file(filename);
      file.RemoveIfBegins("%");  // Remove all comment lines.

      for (const emp::String & line : file) {
        if (line.OnlyWhitespace()) { qbank.NewEntry(); continue; }
        qbank.AddLine(line);
      }
    }
  }

  void Generate() {
    qbank.Validate();
    if (generate_count) {
      qbank.Generate(generate_count, random, include_tags, exclude_tags, 
          require_tags, sample_tags, avoid_files);
    }
  }

  void Print(Format out_format, std::ostream & os=std::cout) const {
    switch (out_format) {
      case Format::QBL:        qbank.Print(os); break;
      case Format::NONE:       qbank.Print(os); break;
      case Format::D2L:        qbank.PrintD2L(os); break;
      case Format::GRADESCOPE: qbank.PrintGradeScope(os, compressed_format); break;
      case Format::LATEX:      qbank.PrintLatex(os); break;
      case Format::WEB:        emp::notify::Error("Web output must go to files."); break;
      case Format::DEBUG:      PrintDebug(os); break;
    }
  }

  void Print() const {
    // If we are supposed to save a log of questions, do so.
    if (log_filename.size()) {
      qbank.LogQuestions(log_filename);
    }

    // If there is no filename, just print to standard out.
    if (!base_filename.size()) { Print(format); return; }

    std::ofstream main_file(base_path + base_filename + extension);
    if (format == Format::WEB) {
      std::ofstream js_file(base_path + base_filename + ".js");
      std::ofstream css_file(base_path + base_filename + ".css");
      PrintWeb(main_file, js_file, css_file);
    }
    else Print(format, main_file);
  }

  void PrintWeb(std::ostream & html_out, std::ostream & js_out, std::ostream & css_out) const {
    // Print the header for the HTML file.
    html_out
    << "<!DOCTYPE html>\n"
    << "<html lang=\"en\">\n"
    << "<head>\n"
    << "  <meta charset=\"UTF-8\">\n"
    << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
    << "  <title>" << title << "</title>\n"
    << "  <link rel=\"stylesheet\" href=\"" << base_filename << ".css\">\n"
    << "</head>\n"
    << "<body>\n"
    << "\n"
    << "<form id=\"quizForm\">\n"
    << "  <h1>" << title << "</h1>\n"
    << "\n";

    qbank.PrintHTML(html_out);

    // Print Footer for the HTML file.
    html_out
    << "  <hr><p>\n"
    << "  Click <b>Check Answers</b> to identify any errors and try again.  Click <b>Show Answers</b> if you also want to know which answer is the correct one.\n"
    << "  </p>\n"
    << "  <button type=\"button\" id=\"checkAnswersBtn\">Check Answers</button>\n"
    << "  <button type=\"button\" id=\"showAnswersBtn\">Show Answers</button>\n"
    << "</form>\n"
    << "<div id=\"results\"></div>\n"
    << "<script src=\"" << base_filename << ".js\"></script>\n"
    << "</body>\n"
    << "</html>\n";

    // Print Header for the JS file.
    js_out
    << "// Fetch all the radio buttons in the quiz\n"
    << "let radioButtons = document.querySelectorAll('input[type=\"radio\"]');\n"
    << "\n"
    << "// Add a click event to each radio button\n"
    << "radioButtons.forEach(button => {\n"
    << "  button.addEventListener('click', function() { clearResults(button.name); });\n"
    << "});\n"
    << "\n"
    << "function clearResults(button_name) {\n"
    << "  // Clear main results\n"
    << "  document.getElementById('results').innerHTML = '';\n"
    << "\n"
    << "  // Clear answers displayed beneath each question\n"
    << "  let answerDiv = document.querySelector(`.answer[data-question=\"${button_name}\"]`);\n"
    << "  answerDiv.innerHTML = \"\";\n"
    << "}\n"
    << "\n"
    << "function PrintResults(show_correct) {"
    << "  event.preventDefault(); // Prevent form from submitting to a server\n"
    << "  let correctAnswers = {\n";

    qbank.PrintJS(js_out);

    // Print Footer for the JS file.
    js_out
    << "  };\n"

    << "\n"
    << "  let userAnswers = {};\n"
    << "  for (let key in correctAnswers) {\n"
    << "    let selectedAnswer = document.querySelector(`input[name=\"${key}\"]:checked`);\n"
    << "    userAnswers[key] = selectedAnswer ? selectedAnswer.value : \"\";\n"
    << "  }\n"
    << "\n"
    << "  let score = 0;\n"
    << "  let results = [];\n"
    << "\n"
    << "  for (let key in correctAnswers) {\n"
    << "    if (userAnswers[key] === correctAnswers[key]) {\n"
    << "      score++;\n"
    << "      results.push({\n"
    << "        question: key,\n"
    << "        status: 1,\n"
    << "        correctAnswer: correctAnswers[key]\n"
    << "      });\n"
    << "    } else {\n"
    << "      results.push({\n"
    << "        question: key,\n"
    << "        status: 0,\n"
    << "        correctAnswer: correctAnswers[key]\n"
    << "      });\n"
    << "    }\n"
    << "  }\n"
    << "\n"
    << "  displayResults(score, results, show_correct);\n"
    << "};\n"
    << "\n"

    << "function displayResults(score, results, show_correct) {\n"
    << "  let resultsDiv = document.getElementById('results');\n"
    << "  resultsDiv.innerHTML = `<p>You got ${score} out of ${Object.keys(results).length} correct!</p>`;\n"
    << "\n"
    << "  // Reset all answer texts\n"
    << "  let answerDivs = document.querySelectorAll('.answer');\n"
    << "  answerDivs.forEach(div => div.innerHTML = \"\");\n"
    << "\n"
    << "  results.forEach(item => {\n"
    << "    let answerDiv = document.querySelector(`.answer[data-question=\"${item.question}\"]`);\n"
    << "    if (item.status === 0) {\n"
    << "      if (show_correct) {\n"
    << "        answerDiv.innerHTML = `<b>Incorrect</b>. The correct answer is: ${item.correctAnswer}`;\n"
    << "      } else {\n"
    << "        answerDiv.innerHTML = `<b>Incorrect</b>.`;\n"
    << "      }\n"
    << "      answerDiv.style.color = \"red\";\n"
    << "    } else {\n"
    << "      answerDiv.innerHTML = `<b>Correct!</b>`;\n"
    << "      answerDiv.style.color = \"green\";\n"
    << "    }\n"
    << "  });\n"
    << "};\n"
    << "\n"
    << "document.getElementById('showAnswersBtn').addEventListener('click', function() {\n"
//    << "document.getElementById('quizForm').addEventListener('submit', function(event) {\n"
    << "  PrintResults(1);\n"
    << "});\n"
    << "\n"
    << "document.getElementById('checkAnswersBtn').addEventListener('click', function() {\n"
    << "  PrintResults(0);\n"
    << "});\n";

    // Print the CSS file.
    css_out
    << "body {\n"
    << "  font-family: Arial, sans-serif;\n"
    << "  margin: 50px;\n"
    << "}\n"
    << "\n"
    << ".question {\n"
    << "  margin-bottom: 20px;\n"
    << "  color: black;\n"
    << "}\n"
    << ".options {\n"
    << "  color: #000088;\n"
    << "}\n"
    << "\n"
    << "label {\n"
    << "  display: block;\n"
    << "  margin-bottom: 5px;\n"
    << "}\n"
    << "\n"
    << "input[type=\"submit\"] {\n"
    << "  padding: 10px 15px;\n"
    << "  background-color: #007BFF;\n"
    << "  color: white;\n"
    << "  border: none;\n"
    << "  cursor: pointer;\n"
    << "}\n"
    << "\n"
    << "input[type=\"submit\"]:hover {\n"
    << "  background-color: #0056b3;\n"
    << "}\n";
  }

  void PrintDebug(std::ostream & os=std::cout) const {
   os << "Question Files: " << emp::MakeLiteral(question_files) << "\n"
      << "Base filename: " << base_filename << "\n"
      << "... extension: " << extension << "\n"
      << "Output Format: " << GetFormatName(format) << "\n"
      << "Include tags: " << emp::MakeLiteral(include_tags) << "\n"
      << "Exclude tags: " << emp::MakeLiteral(exclude_tags) << "\n"
      << "Required tags: " << emp::MakeLiteral(require_tags) << "\n"
      << "Sampled tags: " << emp::MakeLiteral(sample_tags) << "\n"
      << "----------\n";
    qbank.PrintDebug(os);
  }
};

int main(int argc, char * argv[])
{
  if (argc == 1) {
    std::cout << "No arguments provided.\n"
      "Format: " << argv[0] << " question_filename(s) {-o [output_filename]} {-g [question_count]} [OTHER FLAGS]\n"
      "or use '" << argv[0] << " -h' for a more detailed help message."
      << std::endl;
    exit(1);
  }
  QBL qbl(argc, argv);
  qbl.LoadFiles();
  qbl.Generate();
  qbl.UpdateOrder();
  qbl.Print();
}
