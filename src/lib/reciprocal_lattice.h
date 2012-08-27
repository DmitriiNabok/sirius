
namespace sirius {

class reciprocal_lattice : public geometry
{
    private:
        
        /// plane wave cutoff radius (in inverse a.u. of length)
        double pw_cutoff_;
        
        /// FFT wrapper
        FFT3D fft_;

        /// list of G-vector fractional coordinates
        mdarray<int,2> gvec_;

        /// number of G-vectors within plane wave cutoff
        int num_gvec_;

        /// length of G-vectors belonging to the same shell
        std::vector<double> gvec_shell_len_;

        /// mapping between G-vector and shell
        std::vector<int> gvec_shell_;

        /// mapping between linear G-vector index and G-vector coordinates
        mdarray<int,3> index_by_gvec_;

        /// mapping betwee linear G-vector index and position in FFT buffer
        std::vector<int> fft_index_;
        

    public:
    
        reciprocal_lattice() : pw_cutoff_(pw_cutoff_default),
                               num_gvec_(0)
        {
        }
  
        void set_pw_cutoff(double _pw_cutoff)
        {
            pw_cutoff_ = _pw_cutoff;
        }

        void init()
        {
            Timer t("sirius::reciprocal_lattice::init");
            
            int max_frac_coord[3];
            find_translation_limits<reciprocal>(pw_cutoff(), max_frac_coord);
            
            fft_.init(max_frac_coord);
            
            mdarray<int,2> gvec(NULL, 3, fft_.size());
            gvec.allocate();
            std::vector<double> length(fft_.size());

            int ig = 0;
            for (int i = fft_.grid_limits(0, 0); i <= fft_.grid_limits(0, 1); i++)
                for (int j = fft_.grid_limits(1, 0); j <= fft_.grid_limits(1, 1); j++)
                    for (int k = fft_.grid_limits(2, 0); k <= fft_.grid_limits(2, 1); k++)
                    {
                        gvec(0, ig) = i;
                        gvec(1, ig) = j;
                        gvec(2, ig) = k;

                        int fracc[] = {i, j, k};
                        double cartc[3];
                        get_coordinates<cartesian,reciprocal>(fracc, cartc);
                        length[ig] = vector_length(cartc);
                        ig++;
                    }

            std::vector<size_t> reorder(fft_.size());
            gsl_heapsort_index(&reorder[0], &length[0], fft_.size(), sizeof(double), compare_doubles);
           
            gvec_.set_dimensions(3, fft_.size());
            gvec_.allocate();

            num_gvec_ = 0;
            for (int i = 0; i < fft_.size(); i++)
            {
                for (int j = 0; j < 3; j++)
                    gvec_(j, i) = gvec(j, reorder[i]);
                
                if (length[reorder[i]] <= pw_cutoff_)
                    num_gvec_++;
            }

            index_by_gvec_.set_dimensions(dimension(fft_.grid_limits(0, 0), fft_.grid_limits(0, 1)),
                                          dimension(fft_.grid_limits(1, 0), fft_.grid_limits(1, 1)),
                                          dimension(fft_.grid_limits(2, 0), fft_.grid_limits(2, 1)));
            index_by_gvec_.allocate();

            fft_index_.resize(fft_.size());

            for (int ig = 0; ig < fft_.size(); ig++)
            {
                int i0 = gvec_(0, ig);
                int i1 = gvec_(1, ig);
                int i2 = gvec_(2, ig);

                // mapping from G-vector to it's index
                index_by_gvec_(i0, i1, i2) = ig;

                // mapping of FFT buffer linear index
                fft_index_[ig] = fft_.index(i0, i1, i2);
            }

            // find G-shells
            gvec_shell_.resize(num_gvec_);
            gvec_shell_len_.clear();
            for (int ig = 0; ig < num_gvec_; ig++)
            {
                double cartc[3];
                get_coordinates<cartesian,reciprocal>(&gvec_(0, ig), cartc);
                double t = vector_length(cartc);

                if (gvec_shell_len_.empty() || fabs(t - gvec_shell_len_.back()) > 1e-8)
                    gvec_shell_len_.push_back(t);
                 
                gvec_shell_[ig] = gvec_shell_len_.size() - 1;
            }
        }

        void print_info()
        {
            printf("\n");
            printf("plane wave cutoff : %f\n", pw_cutoff_);
            printf("number of G-vectors within the cutoff : %i\n", num_gvec_);
            printf("FFT grid size : %i %i %i   total : %i\n", fft_.size(0), fft_.size(1), fft_.size(2), fft_.size());
            printf("FFT grid limits : %i %i   %i %i   %i %i\n", fft_.grid_limits(0, 0), fft_.grid_limits(0, 1),
                                                                fft_.grid_limits(1, 0), fft_.grid_limits(1, 1),
                                                                fft_.grid_limits(2, 0), fft_.grid_limits(2, 1));
        }
        
        inline FFT3D& fft()
        {
            return fft_;
        }

        inline int index_by_gvec(int i0, int i1, int i2)
        {
            return index_by_gvec_(i0, i1, i2);
        }

        inline int fft_index(int ig)
        {
            return fft_index_[ig];
        }
        
        inline int* gvec(int ig)
        {
            return &gvec_(0, ig);
        }
        
        inline double pw_cutoff()
        {
            return pw_cutoff_;
        }
        
        inline int num_gvec()
        {
            return num_gvec_;
        }
};

};