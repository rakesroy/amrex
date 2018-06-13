
#include <AMReX_MLEBABecLap.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_EBFArrayBox.H>

#include <AMReX_MLEBABecLap_F.H>
#include <AMReX_MLABecLap_F.H>
#include <AMReX_ABec_F.H>

namespace amrex {

MLEBABecLap::MLEBABecLap (const Vector<Geometry>& a_geom,
                          const Vector<BoxArray>& a_grids,
                          const Vector<DistributionMapping>& a_dmap,
                          const LPInfo& a_info,
                          const Vector<EBFArrayBoxFactory const*>& a_factory)
{
    define(a_geom, a_grids, a_dmap, a_info, a_factory);
}

void
MLEBABecLap::define (const Vector<Geometry>& a_geom,
                     const Vector<BoxArray>& a_grids,
                     const Vector<DistributionMapping>& a_dmap,
                     const LPInfo& a_info,
                     const Vector<EBFArrayBoxFactory const*>& a_factory)
{
    BL_PROFILE("MLEBABecLap::define()");

    Vector<FabFactory<FArrayBox> const*> _factory;
    for (auto x : a_factory) {
        _factory.push_back(static_cast<FabFactory<FArrayBox> const*>(x));
    }

    MLCellLinOp::define(a_geom, a_grids, a_dmap, a_info, _factory);

    m_a_coeffs.resize(m_num_amr_levels);
    m_b_coeffs.resize(m_num_amr_levels);
    for (int amrlev = 0; amrlev < m_num_amr_levels; ++amrlev)
    {
        m_a_coeffs[amrlev].resize(m_num_mg_levels[amrlev]);
        m_b_coeffs[amrlev].resize(m_num_mg_levels[amrlev]);
        for (int mglev = 0; mglev < m_num_mg_levels[amrlev]; ++mglev)
        {
            m_a_coeffs[amrlev][mglev].define(m_grids[amrlev][mglev],
                                             m_dmap[amrlev][mglev],
                                             1, 0, MFInfo(), *m_factory[amrlev][mglev]);
            for (int idim = 0; idim < AMREX_SPACEDIM; ++idim)
            {
                const BoxArray& ba = amrex::convert(m_grids[amrlev][mglev],
                                                    IntVect::TheDimensionVector(idim));
                const int ng = 1;
                m_b_coeffs[amrlev][mglev][idim].define(ba,
                                                       m_dmap[amrlev][mglev],
                                                       1, ng, MFInfo(), *m_factory[amrlev][mglev]);
                m_b_coeffs[amrlev][mglev][idim].setVal(0.0);
            }
        }
    }
}

MLEBABecLap::~MLEBABecLap ()
{}

void
MLEBABecLap::setScalars (Real a, Real b)
{
    m_a_scalar = a;
    m_b_scalar = b;
    if (a == 0.0)
    {
        for (int amrlev = 0; amrlev < m_num_amr_levels; ++amrlev)
        {
            m_a_coeffs[amrlev][0].setVal(0.0);
        }
    }
}

void
MLEBABecLap::setACoeffs (int amrlev, const MultiFab& alpha)
{
    MultiFab::Copy(m_a_coeffs[amrlev][0], alpha, 0, 0, 1, 0);
}

void
MLEBABecLap::setBCoeffs (int amrlev, const std::array<MultiFab const*,AMREX_SPACEDIM>& beta)
{
    for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
        MultiFab::Copy(m_b_coeffs[amrlev][0][idim], *beta[idim], 0, 0, 1, 0);
        m_b_coeffs[amrlev][0][idim].FillBoundary(m_geom[amrlev][0].periodicity());
    }
}

void
MLEBABecLap::averageDownCoeffs ()
{
    for (int amrlev = m_num_amr_levels-1; amrlev > 0; --amrlev)
    {
        auto& fine_a_coeffs = m_a_coeffs[amrlev];
        auto& fine_b_coeffs = m_b_coeffs[amrlev];
        
        averageDownCoeffsSameAmrLevel(fine_a_coeffs, fine_b_coeffs);
        averageDownCoeffsToCoarseAmrLevel(amrlev);
    }

    averageDownCoeffsSameAmrLevel(m_a_coeffs[0], m_b_coeffs[0]);
}

void
MLEBABecLap::averageDownCoeffsSameAmrLevel (Vector<MultiFab>& a,
                                                Vector<std::array<MultiFab,AMREX_SPACEDIM> >& b)
{
    int nmglevs = a.size();
    for (int mglev = 1; mglev < nmglevs; ++mglev)
    {
        amrex::Abort("averageDownCoeffsSameAmrLevel: todo");

        if (m_a_scalar == 0.0)
        {
            a[mglev].setVal(0.0);
        }
        else
        {
            amrex::average_down(a[mglev-1], a[mglev], 0, 1, mg_coarsen_ratio);
        }
        
        Vector<const MultiFab*> fine {AMREX_D_DECL(&(b[mglev-1][0]),
                                                   &(b[mglev-1][1]),
                                                   &(b[mglev-1][2]))};
        Vector<MultiFab*> crse {AMREX_D_DECL(&(b[mglev][0]),
                                             &(b[mglev][1]),
                                             &(b[mglev][2]))};
        IntVect ratio {mg_coarsen_ratio};
        amrex::average_down_faces(fine, crse, ratio, 0);
    }
}

void
MLEBABecLap::averageDownCoeffsToCoarseAmrLevel (int flev)
{
    amrex::Abort("averageDownCoeffsToCoarseAmrLevel: todo");
}

void
MLEBABecLap::prepareForSolve ()
{
    BL_PROFILE("MLABecLaplacian::prepareForSolve()");

    MLCellLinOp::prepareForSolve();
    
    averageDownCoeffs();

    m_is_singular.clear();
    m_is_singular.resize(m_num_amr_levels, false);
    auto itlo = std::find(m_lobc.begin(), m_lobc.end(), BCType::Dirichlet);
    auto ithi = std::find(m_hibc.begin(), m_hibc.end(), BCType::Dirichlet);
    if (itlo == m_lobc.end() && ithi == m_hibc.end())
    {  // No Dirichlet
        for (int alev = 0; alev < m_num_amr_levels; ++alev)
        {
            if (m_domain_covered[alev])
            {
                if (m_a_scalar == 0.0)
                {
                    m_is_singular[alev] = true;
                }
                else
                {
                    Real asum = m_a_coeffs[alev].back().sum();
                    Real amax = m_a_coeffs[alev].back().norm0();
                    m_is_singular[alev] = (asum <= amax * 1.e-12);
                }
            }
        }
    }
}

void
MLEBABecLap::Fapply (int amrlev, int mglev, MultiFab& out, const MultiFab& in) const
{
    BL_PROFILE("MLEBABecLap::Fapply()");

    const MultiFab& acoef = m_a_coeffs[amrlev][mglev];
    AMREX_D_TERM(const MultiFab& bxcoef = m_b_coeffs[amrlev][mglev][0];,
                 const MultiFab& bycoef = m_b_coeffs[amrlev][mglev][1];,
                 const MultiFab& bzcoef = m_b_coeffs[amrlev][mglev][2];);

    const Real* dxinv = m_geom[amrlev][mglev].InvCellSize();

    auto factory = dynamic_cast<EBFArrayBoxFactory const*>(&(in.Factory()));
    const FabArray<EBCellFlagFab>* flags = (factory) ? &(factory->getMultiEBCellFlagFab()) : nullptr;
    const MultiFab* vfrac = (factory) ? &(factory->getVolFrac()) : nullptr;
    auto area = (factory) ? factory->getAreaFrac()
        : Array<const MultiCutFab*,AMREX_SPACEDIM>{AMREX_D_DECL(nullptr,nullptr,nullptr)};
    auto fcent = (factory) ? factory->getFaceCent()
        : Array<const MultiCutFab*,AMREX_SPACEDIM>{AMREX_D_DECL(nullptr,nullptr,nullptr)};

#ifdef _OPENMP
#pragma omp parallel
#endif
    for (MFIter mfi(out, MFItInfo().EnableTiling().SetDynamic(true)); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.tilebox();
        const FArrayBox& xfab = in[mfi];
        FArrayBox& yfab = out[mfi];
        const FArrayBox& afab = acoef[mfi];
        AMREX_D_TERM(const FArrayBox& bxfab = bxcoef[mfi];,
                     const FArrayBox& byfab = bycoef[mfi];,
                     const FArrayBox& bzfab = bzcoef[mfi];);

        auto fabtyp = (flags) ? (*flags)[mfi].getType(bx) : FabType::regular;

        if (fabtyp == FabType::covered) {
            yfab.setVal(0.0, bx, 0, 1);
        } else if (fabtyp == FabType::regular) {
            amrex_mlabeclap_adotx(BL_TO_FORTRAN_BOX(bx),
                                  BL_TO_FORTRAN_ANYD(yfab),
                                  BL_TO_FORTRAN_ANYD(xfab),
                                  BL_TO_FORTRAN_ANYD(afab),
                                  AMREX_D_DECL(BL_TO_FORTRAN_ANYD(bxfab),
                                               BL_TO_FORTRAN_ANYD(byfab),
                                               BL_TO_FORTRAN_ANYD(bzfab)),
                                  dxinv, m_a_scalar, m_b_scalar);
        } else {
            amrex_mlebabeclap_adotx(BL_TO_FORTRAN_BOX(bx),
                                    BL_TO_FORTRAN_ANYD(yfab),
                                    BL_TO_FORTRAN_ANYD(xfab),
                                    BL_TO_FORTRAN_ANYD(afab),
                                    AMREX_D_DECL(BL_TO_FORTRAN_ANYD(bxfab),
                                                 BL_TO_FORTRAN_ANYD(byfab),
                                                 BL_TO_FORTRAN_ANYD(bzfab)),
                                    BL_TO_FORTRAN_ANYD((*flags)[mfi]),
                                    BL_TO_FORTRAN_ANYD((*vfrac)[mfi]),
                                    AMREX_D_DECL(BL_TO_FORTRAN_ANYD((*area[0])[mfi]),
                                                 BL_TO_FORTRAN_ANYD((*area[1])[mfi]),
                                                 BL_TO_FORTRAN_ANYD((*area[2])[mfi])),
                                    AMREX_D_DECL(BL_TO_FORTRAN_ANYD((*fcent[0])[mfi]),
                                                 BL_TO_FORTRAN_ANYD((*fcent[1])[mfi]),
                                                 BL_TO_FORTRAN_ANYD((*fcent[2])[mfi])),
                                    dxinv, m_a_scalar, m_b_scalar);
        }
    }

    VisMF::Write(in, "in");
    VisMF::Write(out, "out");

    amrex::Abort("Fapply: todo");
}

void
MLEBABecLap::Fsmooth (int amrlev, int mglev, MultiFab& sol, const MultiFab& rsh, int redblack) const
{
    amrex::Abort("Fsmooth: todo");
}

void
MLEBABecLap::FFlux (int amrlev, const MFIter& mfi, const std::array<FArrayBox*,AMREX_SPACEDIM>& flux,
                    const FArrayBox& sol, const int face_only) const
{
    amrex::Abort("FFlux: todo");
}

void
MLEBABecLap::normalize (int amrlev, int mglev, MultiFab& mf) const
{
    amrex::Abort("normalize: todo");
}

}
