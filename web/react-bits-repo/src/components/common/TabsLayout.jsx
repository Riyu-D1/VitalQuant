import React, { useEffect, useMemo, useState } from 'react';
import ContributionSection from './ContributionSection';
import TabsFooter from './TabsFooter';

import { Tabs, Icon, Flex, Tooltip, Box } from '@chakra-ui/react';
import { FiCode, FiEye } from 'react-icons/fi';
import { RiHeartFill, RiHeartLine, RiLightbulbLine } from 'react-icons/ri';
import { RotateCcw } from 'lucide-react';
import { useParams } from 'react-router-dom';
import { toast } from 'sonner';
import { toggleSavedComponent, isComponentSaved } from '../../utils/favorites';
import { useComponentPropsContext } from '../../hooks/useComponentPropsContext';
import { colors } from '../../constants/colors';

const TAB_STYLE_PROPS = {
  flex: '0 0 auto',
  border: `1px solid ${colors.borderSecondary}`,
  borderRadius: '15px',
  fontSize: '14px',
  h: 10,
  px: 4,
  color: '#ffffff',
  justifyContent: 'center',
  _hover: { bg: colors.bgHover },
  _selected: { bg: colors.bgElevated, color: colors.accent }
};

const TabsLayout = ({ children, className }) => {
  const { category, subcategory } = useParams();
  const { hasChanges, resetProps } = useComponentPropsContext();

  const { favoriteKey, componentName } = useMemo(() => {
    if (!category || !subcategory) return null;

    const toPascal = str =>
      str
        .split('-')
        .map(word => word.charAt(0).toUpperCase() + word.slice(1))
        .join('');

    const categoryName = toPascal(category);
    const componentName = toPascal(subcategory);
    return { favoriteKey: `${categoryName}/${componentName}`, componentName };
  }, [category, subcategory]) || { favoriteKey: null, componentName: null };

  const [isSaved, setIsSaved] = useState(false);

  useEffect(() => {
    if (!favoriteKey) return;
    setIsSaved(isComponentSaved(favoriteKey));
  }, [favoriteKey]);

  const toggleFavorite = () => {
    if (!favoriteKey) return;
    const { saved } = toggleSavedComponent(favoriteKey);
    setIsSaved(saved);

    const nameEl = (
      <span>
        {' '}
        <span style={{ color: colors.accent, fontWeight: 700 }}>&lt;{componentName} /&gt;</span>
      </span>
    );

    if (saved) toast.success?.(<>Added {nameEl} to favorites</>) ?? toast(<>Added {nameEl} to favorites</>);
    else toast.error?.(<>Removed {nameEl} from favorites</>) ?? toast(<>Removed {nameEl} from favorites</>);
  };
  const contentMap = {
    PreviewTab: null,
    CodeTab: null
  };

  React.Children.forEach(children, child => {
    if (!child) return;
    if (child.type === PreviewTab) contentMap.PreviewTab = child;
    if (child.type === CodeTab) contentMap.CodeTab = child;
  });

  return (
    <Tabs.Root w="100%" variant="plain" lazyMount defaultValue="preview" className={className}>
      <Tabs.List w="100%">
        <Flex gap={2} justifyContent="space-between" alignItems="flex-start" w="100%" wrap="wrap">
          <Flex gap={2} wrap="wrap" minW="0" flex="1">
            <Tabs.Trigger value="preview" {...TAB_STYLE_PROPS}>
              <Icon as={FiEye} /> Preview
            </Tabs.Trigger>

            <Tabs.Trigger value="code" {...TAB_STYLE_PROPS}>
              <Icon as={FiCode} /> Code
            </Tabs.Trigger>
          </Flex>

          <Flex alignItems="center" gap={2} flexShrink={0}>
            {hasChanges && (
              <Box
                as="button"
                aria-label="Reset to defaults"
                onClick={resetProps}
                display="flex"
                cursor="pointer"
                alignItems="center"
                justifyContent="center"
                gap={2}
                {...TAB_STYLE_PROPS}
              >
                <RotateCcw size={16} color="#fff" /> Reset
              </Box>
            )}

            {favoriteKey && category !== 'get-started' && (
              <Tooltip.Root openDelay={250} closeDelay={100} positioning={{ placement: 'left', gutter: 8 }}>
                <Tooltip.Trigger asChild>
                  <Box
                    as="button"
                    aria-label="Add to Favorites"
                    onClick={toggleFavorite}
                    aria-pressed={isSaved}
                    display="flex"
                    cursor="pointer"
                    alignItems="center"
                    gap={2}
                    {...TAB_STYLE_PROPS}
                    w={10}
                    bg={isSaved ? 'linear-gradient(-135deg, rgba(124, 58, 237, 1), rgba(75, 58, 255, 0.6))' : undefined}
                    _hover={isSaved ? { filter: 'brightness(0.9)' } : TAB_STYLE_PROPS._hover}
                    border={isSaved ? 'none' : TAB_STYLE_PROPS.border}
                  >
                    <Icon as={isSaved ? RiHeartFill : RiHeartLine} color="#fff" boxSize={4} />
                  </Box>
                </Tooltip.Trigger>
                <Tooltip.Positioner>
                  <Tooltip.Content
                    bg={colors.bgBody}
                    border={`1px solid ${colors.borderPrimary}`}
                    color={colors.accent}
                    fontSize="12px"
                    fontWeight="500"
                    lineHeight="0"
                    px={4}
                    whiteSpace="nowrap"
                    h={10}
                    borderRadius="15px"
                    display="flex"
                    alignItems="center"
                    justifyContent="center"
                    textAlign="center"
                    pointerEvents="none"
                  >
                    {isSaved ? 'Remove from Favorites' : 'Add to Favorites'}
                  </Tooltip.Content>
                </Tooltip.Positioner>
              </Tooltip.Root>
            )}

            <Tabs.Trigger className="contribute-tab" value="contribute" {...TAB_STYLE_PROPS}>
              <Icon as={RiLightbulbLine} /> Contribute
            </Tabs.Trigger>
          </Flex>
        </Flex>
      </Tabs.List>

      <Tabs.Content pt={0} value="preview">
        {contentMap.PreviewTab}
      </Tabs.Content>
      <Tabs.Content pt={0} value="code">
        {contentMap.CodeTab}
      </Tabs.Content>

      <Tabs.Content pt={0} value="contribute">
        <ContributionSection />
      </Tabs.Content>

      <TabsFooter />
    </Tabs.Root>
  );
};

export const PreviewTab = ({ children }) => <>{children}</>;
export const CodeTab = ({ children }) => <>{children}</>;

export { TabsLayout };
