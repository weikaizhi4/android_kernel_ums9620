// SPDX-License-Identifier: GPL-2.0

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define SPRD_VPU_PD_ON		0
#define SPRD_VPU_PD_OFF	7
#define SPRD_VPU_PD_POLL_DELAY_US	100
#define SPRD_VPU_PD_POLL_TIMEOUT_US	(1000 * 1000)

struct sprd_vpu_pm_domain {
	struct device *dev;
	struct generic_pm_domain pd;
	struct regmap *force_shutdown_map;
	struct regmap *auto_shutdown_map;
	struct regmap *status_map;
	struct regmap *pixelpll_map;
	struct regmap *domain_eb_map;
	unsigned int force_shutdown_reg;
	unsigned int force_shutdown_mask;
	unsigned int auto_shutdown_reg;
	unsigned int auto_shutdown_mask;
	unsigned int status_reg;
	unsigned int status_mask;
	unsigned int pixelpll_reg;
	unsigned int pixelpll_mask;
	unsigned int domain_eb_reg;
	unsigned int domain_eb_mask;
};

static int sprd_vpu_pd_get_syscon(struct device_node *np, const char *name,
				  struct regmap **map, unsigned int *reg,
				  unsigned int *mask)
{
	unsigned int args[2];

	*map = syscon_regmap_lookup_by_phandle_args(np, name, 2, args);
	if (IS_ERR(*map))
		return PTR_ERR(*map);

	*reg = args[0];
	*mask = args[1];

	return 0;
}

static int sprd_vpu_pd_power_on(struct generic_pm_domain *pd)
{
	struct sprd_vpu_pm_domain *domain =
		container_of(pd, struct sprd_vpu_pm_domain, pd);
	u32 state;
	int ret;

	ret = regmap_update_bits(domain->force_shutdown_map,
			domain->force_shutdown_reg, domain->force_shutdown_mask, 0);
	if (ret)
		return ret;

	ret = regmap_update_bits(domain->auto_shutdown_map,
			domain->auto_shutdown_reg, domain->auto_shutdown_mask, 0);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(domain->status_map, domain->status_reg,
			state,
			((state & domain->status_mask) >>
			 __ffs(domain->status_mask)) == SPRD_VPU_PD_ON,
			SPRD_VPU_PD_POLL_DELAY_US,
			SPRD_VPU_PD_POLL_TIMEOUT_US);
	if (ret)
		return ret;

	if (domain->pixelpll_map) {
		ret = regmap_update_bits(domain->pixelpll_map,
				domain->pixelpll_reg, domain->pixelpll_mask,
				domain->pixelpll_mask);
		if (ret)
			return ret;
	}

	if (domain->domain_eb_map)
		return regmap_update_bits(domain->domain_eb_map,
			domain->domain_eb_reg, domain->domain_eb_mask,
			domain->domain_eb_mask);

	return 0;
}

static int sprd_vpu_pd_power_off(struct generic_pm_domain *pd)
{
	struct sprd_vpu_pm_domain *domain =
		container_of(pd, struct sprd_vpu_pm_domain, pd);
	u32 state;
	int ret;

	if (domain->domain_eb_map) {
		ret = regmap_update_bits(domain->domain_eb_map,
				domain->domain_eb_reg, domain->domain_eb_mask, 0);
		if (ret)
			return ret;
	}

	ret = regmap_update_bits(domain->auto_shutdown_map,
			domain->auto_shutdown_reg, domain->auto_shutdown_mask,
			domain->auto_shutdown_mask);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(domain->status_map, domain->status_reg,
			state,
			((state & domain->status_mask) >>
			 __ffs(domain->status_mask)) == SPRD_VPU_PD_OFF,
			SPRD_VPU_PD_POLL_DELAY_US,
			SPRD_VPU_PD_POLL_TIMEOUT_US);
	if (ret)
		return ret;

	if (domain->pixelpll_map)
		return regmap_update_bits(domain->pixelpll_map,
			domain->pixelpll_reg, domain->pixelpll_mask, 0);

	return 0;
}

static int sprd_vpu_pd_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sprd_vpu_pm_domain *domain;
	const char *label;
	int ret;

	domain = devm_kzalloc(&pdev->dev, sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return -ENOMEM;

	domain->dev = &pdev->dev;

	ret = sprd_vpu_pd_get_syscon(np, "pmu-vpu-force-shutdown-syscon",
			&domain->force_shutdown_map, &domain->force_shutdown_reg,
			&domain->force_shutdown_mask);
	if (ret)
		return ret;

	ret = sprd_vpu_pd_get_syscon(np, "pmu-vpu-auto-shutdown-syscon",
			&domain->auto_shutdown_map, &domain->auto_shutdown_reg,
			&domain->auto_shutdown_mask);
	if (ret)
		return ret;

	ret = sprd_vpu_pd_get_syscon(np, "pmu-pwr-status-syscon",
			&domain->status_map, &domain->status_reg,
			&domain->status_mask);
	if (ret)
		return ret;

	if (of_find_property(np, "pmu-apb-pixelpll-syscon", NULL)) {
		ret = sprd_vpu_pd_get_syscon(np, "pmu-apb-pixelpll-syscon",
				&domain->pixelpll_map, &domain->pixelpll_reg,
				&domain->pixelpll_mask);
		if (ret)
			return ret;
	}

	if (of_find_property(np, "vpu-domain-eb-syscon", NULL)) {
		ret = sprd_vpu_pd_get_syscon(np, "vpu-domain-eb-syscon",
				&domain->domain_eb_map, &domain->domain_eb_reg,
				&domain->domain_eb_mask);
		if (ret)
			return ret;
	}

	if (of_property_read_string(np, "label", &label))
		label = np->name;

	domain->pd.name = devm_kstrdup(&pdev->dev, label, GFP_KERNEL);
	if (!domain->pd.name)
		return -ENOMEM;
	domain->pd.power_on = sprd_vpu_pd_power_on;
	domain->pd.power_off = sprd_vpu_pd_power_off;

	pm_genpd_init(&domain->pd, NULL, true);

	ret = of_genpd_add_provider_simple(np, &domain->pd);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, domain);
	return 0;
}

static int sprd_vpu_pd_remove(struct platform_device *pdev)
{
	of_genpd_del_provider(pdev->dev.of_node);
	return 0;
}

static const struct of_device_id sprd_vpu_pd_of_match[] = {
	{ .compatible = "sprd,vpu-pd" },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_vpu_pd_of_match);

static struct platform_driver sprd_vpu_pd_driver = {
	.probe = sprd_vpu_pd_probe,
	.remove = sprd_vpu_pd_remove,
	.driver = {
		.name = "sprd-vpu-pd",
		.of_match_table = sprd_vpu_pd_of_match,
	},
};
module_platform_driver(sprd_vpu_pd_driver);

MODULE_DESCRIPTION("Unisoc VPU power domain driver");
MODULE_LICENSE("GPL v2");
