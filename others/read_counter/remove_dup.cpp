#include <stdio.h>
#include <iostream>
#include <string>
#include <string.h>
#include <assert.h>
#include <htslib/sam.h>
#include <htslib/kstring.h>
#include <map>
#include "my_exception.h"
#include <boost/program_options.hpp>
#include <regex>
#include <regex.h>

namespace po = boost::program_options;

class remove_dupcc {

    private:
        typedef struct flagvals_s flagvals;
        po::options_description desc;
    public:
        void print_help();
        bool parse_args(int argc, char* argv[]);
        flagvals get_flagvals(int flag);
        std::string get_format(std::string lfname_str, std::string open_mode);
        htsFile* get_hts_handle(std::string lfname_str, std::string format);
        void bam_handle_init(std::string fname_str, const bam_hdr_t* lhdr, 
            htsFile*& lfp_out, bam_hdr_t*& lhdr_out, std::string open_mode);
        void initialize();
        void free_vars();
        bool has_suffix(const std::string &str, const std::string &suffix);
        bool check_write(bam_hdr_t *lhdr, bam1_t *lread1, bam1_t *lread2);
        void start_rts_paired();
        void main_func();

    private:
        std::string infile_str;
        std::string outfile_str;
        std::string dfile_str;
        htsFile *fp_in = NULL;
        htsFile *fp_out = NULL;
        htsFile *fp_d = NULL;
        bam_hdr_t *hdr_in = NULL;
        bam_hdr_t *hdr_out = NULL;
        bam_hdr_t *hdr_d = NULL;
        long read_limit;
};

struct flagvals_s{
    int is_paired;
    int is_proper_paired;
    int is_unmapped;
    int is_mate_unmapped;
    int is_reverse;
    int is_mate_reverse;
    int is_read1;
    int is_read2;
    int is_dup;
};

void remove_dupcc::print_help() {
    std::cout << desc << "\n";
    std::cout << "Usage: filter_sam --infile <sam/bam> --outfile <output file>"
       "--dfile <broken read file>"
       "\n\n";
}


bool remove_dupcc::parse_args(int argc, char* argv[]) {
    bool all_set = true;
  
    desc.add_options()
        ("help,h", "produce help message")
        ("infile,i", po::value<std::string>(&infile_str), "input sam/bam file.")
        ("outfile,o", po::value<std::string>(&outfile_str), "output sam/bam file.")
        ("dfile,d", po::value<std::string>(&dfile_str), "Dup reads file.")
        ("read_limit,r", po::value(&read_limit)->default_value(-1),
            "Pair of reads to process")
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    
    if (vm.count("help")) {
        return 0;
    } else {
    }

    if (vm.count("infile")) {
        std::cout << "Infile is set to: " << infile_str << "\n";
    } else {
        all_set = false;
        std::cout << "Error: infile is not set.\n";
    }

    if (vm.count("outfile")) {
        std::cout << "Outfile is set to " << outfile_str << "\n";
    } else {
        all_set = false;
        std::cout << "Error: outfile is not set.\n";
    }

    if (vm.count("dfile")) {
        std::cout << "dfile is set to " << dfile_str << "\n";
    } else {
        all_set = false;
        std::cout << "Error: dfile is not set.\n";
    }

    return all_set;
}


remove_dupcc::flagvals remove_dupcc::get_flagvals(int flag) {

    flagvals lvals;
    lvals.is_paired = flag & BAM_FPAIRED;
    lvals.is_proper_paired = flag & BAM_FPROPER_PAIR;
    lvals.is_unmapped = flag & BAM_FUNMAP;
    lvals.is_mate_unmapped = flag & BAM_FMUNMAP;
    lvals.is_reverse = flag & BAM_FREVERSE;
    lvals.is_mate_reverse = flag & BAM_FMREVERSE;
    lvals.is_read1 = flag & BAM_FREAD1;
    lvals.is_read2 = flag & BAM_FREAD2;
    lvals.is_dup = flag & BAM_FDUP;

    return lvals;

}

// Note that this function does a dulication; so there mighe be memory
// issue that need to be taken care of.
// Also, it is passed a lhdr as input, if that is null the function open
// the input file and assign the header to it. Otherwise, the function 
// copies the header to the output header.

std::string remove_dupcc::get_format(std::string lfname_str, std::string open_mode) {

    std::string format;
    if (has_suffix(lfname_str, "sam")) {
        format = open_mode;
    } else if (has_suffix(lfname_str, "bam")) {
        format = open_mode + "b";
    } else {
        std::string lstr = "File with illegal suffix: " + lfname_str + "\n";
        throw std::runtime_error(lstr);
    }
    return format;
}


htsFile* remove_dupcc::get_hts_handle(std::string lfname_str, std::string format) {
    htsFile* lfp = NULL;
    const char* fname_cstr = lfname_str.c_str();
    const char* format_cstr = format.c_str();
    printf("%s\t%s\n", fname_cstr, format_cstr);
    std::cout << "format from get_hts_handle: " << format << "\n";
    if (!(lfp = sam_open(fname_cstr, format_cstr))) {
        std::cout << "Error in opening the file: " << lfname_str << "\n";
    } else {
        std::cout << "Successfully opened the file: " << lfname_str << "\n";
    }
    return lfp;

}

// If the input header is NOT null, it duplicates the input header to the 
// output header, otherwise it directly copies the header from the
// input file.

void remove_dupcc::bam_handle_init(std::string fname_str, const bam_hdr_t* lhdr,
        htsFile*& lfp_out, bam_hdr_t*& lhdr_out, std::string open_mode) {

    std::string format = get_format(fname_str, open_mode);
    htsFile* lfp = get_hts_handle(fname_str, format);
    lfp_out = lfp;

    if (open_mode == "r") {
        // Create one and return
        lhdr_out = sam_hdr_read(lfp);
    } else {
        // Create a duplicate and return
        lhdr_out = bam_hdr_dup(lhdr);
    }


    if (open_mode == "w") {
        if (sam_hdr_write(lfp_out, lhdr_out) < 0 ) {
            std::cout << "Error in sam_hdr_write." << "\n";
        } else {
            std::cout << "Wrote the header successfully." << "\n";
        }

    }
}

void remove_dupcc::initialize() {

    bam_handle_init(infile_str, NULL, fp_in, hdr_in, "r");
    bam_handle_init(outfile_str, hdr_in, fp_out, hdr_out, "w");
    bam_handle_init(dfile_str, hdr_in, fp_d, hdr_d, "w");
}

void remove_dupcc::free_vars() {

    bam_hdr_destroy(hdr_in);
    bam_hdr_destroy(hdr_out);
    bam_hdr_destroy(hdr_d);
    sam_close(fp_in);
    sam_close(fp_out);
    sam_close(fp_d);
}

bool remove_dupcc::has_suffix(const std::string &str, const std::string &suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}


bool remove_dupcc::check_write(bam_hdr_t *lhdr, bam1_t *lread1, bam1_t *lread2) {

    uint16_t flag1 = (lread1 -> core).flag;
    uint16_t flag2 = (lread2 -> core).flag;

    flagvals lfvals1 = get_flagvals(flag1);
    flagvals lfvals2 = get_flagvals(flag2);

    if (!lfvals1.is_dup && !lfvals2.is_dup) {
        return true;
    } else {
        // Either of the reads is duplicate
        return false;
    }

}


void remove_dupcc::start_rts_paired() {

    unsigned long total_pair = 0;
    unsigned long valid_pair = 0;
    unsigned long invalid_pair = 0;

    bam1_t* read1 = bam_init1();
    bam1_t* read2 = bam_init1();

    

    // Here fp is for the input bam file and lhdr is header for the input file.
    while(sam_read1(fp_in, hdr_in, read1) >= 0) {

        if (sam_read1(fp_in, hdr_in, read2) < 0) {
            std::string lstr = "Failed to read second read.\n";
            throw std::runtime_error(lstr);
        }

        bool will_write = check_write(hdr_in, read1, read2);
        if (will_write) {
            // write the pair of reads to the outfile; note that file pointer
            // and header would change to output
            if (sam_write1(fp_out, hdr_out, read1) < 0) {
                std::cout << "Problem with sam_write1" << "\n";
            } 
            if (sam_write1(fp_out, hdr_out, read2) < 0) {
                std::cout << "Problem with sam_write1" << "\n";
            }

            valid_pair++;

        } else {
            // Update the header, mark with BAM_FUNMAP and BAM_FMUNMAP so that both the reads
            // and their mates are unmapped.

            
            invalid_pair++;

            // Then write the original reads in the broken_file
            if (sam_write1(fp_d, hdr_d, read1) < 0) {
                std::cout << "Problem with sam_write1" << "\n";
            } 

            if (sam_write1(fp_d, hdr_d, read2) < 0) {
                std::cout << "Problem with sam_write1" << "\n";
            }

        }

        total_pair++;
        if (total_pair == read_limit) {
            break;
        }
    }

    std::cout << "total_pair: " << std::to_string(total_pair) << "\n";
    std::cout << "nondup_pair: " << std::to_string(valid_pair) << "\n";
    std::cout << "dup_pair: " << std::to_string(invalid_pair) << "\n";
   
    bam_destroy1(read1); 
    bam_destroy1(read2); 
}


void remove_dupcc::main_func() {
    // This would only work on the paired end data.
    start_rts_paired();
}

int main(int argc, char** argv) {

    remove_dupcc rdc;
    bool all_set = true;
    try {
        all_set = rdc.parse_args(argc, argv);
    } catch(std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;

    } catch (...) {
        return 0;
    }

    if (!all_set) {
        rdc.print_help();
        return 0;
    }
    try {
        rdc.initialize();
        rdc.main_func();
        rdc.free_vars();
    } catch (const std::runtime_error& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

